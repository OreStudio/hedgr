/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/*
 * What is deliberately not tested here: a live session against NATS. These
 * tests pass on a machine with no broker, no certificates and no IAM service,
 * because that is the machine a developer runs them on. The session test below
 * points a session at a port nothing listens on, so the worker's own connect
 * fails and the event path is exercised end to end without a deployment on the
 * other end. Anything that needs an answered round trip -- sign-in, refresh,
 * window data -- is covered by the running application against a real
 * instance, and pinning it here would only make the suite depend on a daemon.
 */

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/identity.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::chart_state;
using hedgr::model::domain::identity;
using hedgr::model::domain::instance;
using hedgr::model::domain::session_status;
using hedgr::model::domain::tier;
using hedgr::model::domain::window_kind;
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
using hedgr::runtime::async::executor;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * Every test gets its own path, because a shared one would make the suite pass
 * or fail on the order the tests happen to run in.
 */
class temporary_database final {
public:
    explicit temporary_database(std::string_view tag)
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.{}.{}.sqlite", tag, next_serial())) {
        erase();
    }

    ~temporary_database() {
        erase();
    }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    void erase() const {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    std::filesystem::path path_;
};

/**
 * @brief The options the application builds from the environment.
 */
[[nodiscard]] session_options seed_options() {
    session_options options;
    options.nats_url = "nats://localhost:20405";
    options.subject_prefix = "ores.dev.brave_hopper";
    return options;
}

/**
 * @brief Polls until @p ready says so, or the limit passes.
 *
 * A bounded wait rather than a sleep, so a failure fails the test instead of
 * hanging the suite on a worker that never ran.
 */
[[nodiscard]] bool wait_until(const std::function<bool()>& ready,
    std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

/** @brief An instance row with only what a test cares about named. */
[[nodiscard]] instance make_instance(std::string id, std::string name,
    std::string subject_prefix) {
    instance row;
    row.id = std::move(id);
    row.name = std::move(name);
    row.tier = tier::staging;
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = std::move(subject_prefix);
    return row;
}

/** @brief An identity row belonging to @p instance_id. */
[[nodiscard]] identity make_identity(std::string id, std::string instance_id) {
    identity row;
    row.id = std::move(id);
    row.instance_id = std::move(instance_id);
    row.label = "ops read-only";
    row.principal = "alice@example.test";
    return row;
}

void push_command(instance_manager& manager, command request) {
    manager.commands().push(std::move(request));
    manager.pump();
}

/**
 * @brief The coroutine the spawn test hands to the pool.
 *
 * Written as a named function rather than a lambda on purpose. clang 21 elides
 * the frame of a *lambda* coroutine onto the caller's stack even when the
 * awaitable is passed on to another function that outlives the call, and the
 * pool thread then resumes a frame whose stack slot is already gone
 * (AddressSanitizer reports "stack-use-after-scope"). A named coroutine's frame
 * is a return value, so the compiler leaves it on the heap as it must.
 */
boost::asio::awaitable<void> set_flag(std::atomic<bool>& flag) {
    flag.store(true);
    co_return;
}

}

TEST_CASE("the default pool size is between two and four threads", "[runtime]") {
    const auto count = executor::default_thread_count();

    REQUIRE(count >= 2);
    REQUIRE(count <= 4);
}

TEST_CASE("an executor can be built and torn down", "[runtime]") {
    /*
     * The join test: if stop() did not release the work guard, or if the
     * threads were not joinable, this would never return.
     */
    executor pool(3);
    REQUIRE(pool.thread_count() == 3);

    pool.stop();
    pool.stop();
    REQUIRE(pool.thread_count() == 0);
}

TEST_CASE("spawn runs a coroutine on the pool", "[runtime]") {
    /* Declared first so the pool is joined before the flag goes away. */
    std::atomic<bool> ran{false};

    executor pool(2);
    pool.spawn(set_flag(ran));

    REQUIRE(wait_until([&ran] { return ran.load(); }, std::chrono::seconds(5)));
}

TEST_CASE("start opens the store and seeds one development instance", "[runtime]") {
    const temporary_database database("seed");

    instance_manager manager;
    const auto started = manager.start(database.path(), seed_options());

    REQUIRE(started.has_value());
    REQUIRE(manager.is_started());
    REQUIRE(manager.instances().size() == 1);

    const auto& row = manager.instances().rows().front();
    REQUIRE(row.name == "local development");
    REQUIRE(row.tier == tier::development);
    REQUIRE(row.nats_url == "nats://localhost:20405");
    REQUIRE(row.subject_prefix == "ores.dev.brave_hopper");
    REQUIRE_FALSE(row.created_at.empty());

    const auto window = manager.windows().find("instance-manager");
    REQUIRE(window.has_value());
    REQUIRE(manager.windows().kind(*window) == window_kind::instance_manager);
    REQUIRE_FALSE(manager.windows().session_of(*window).has_value());
    REQUIRE(manager.windows().size() == 1);

    manager.stop();
}

TEST_CASE("starting twice leaves one instance", "[runtime]") {
    const temporary_database database("idempotent");

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(manager.instances().size() == 1);
    }

    /* A second manager over the same file must not seed a second instance. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.instances().size() == 1);
    REQUIRE(reopened.instances().rows().front().tier == tier::development);
    REQUIRE(reopened.windows().find("instance-manager").has_value());
    reopened.stop();
}

TEST_CASE("a save_instance command adds a row and replaces it in place",
    "[runtime]") {
    const temporary_database database("save-instance");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    command request;
    request.kind = command_kind::save_instance;
    request.instance_row = make_instance("inst-alpha", "Alpha", "ores.staging.alpha");
    request.instance_id = request.instance_row.id;
    push_command(manager, request);

    REQUIRE(manager.instances().size() == 2);
    REQUIRE(manager.instances().find("inst-alpha") != nullptr);
    REQUIRE(manager.instances().find("inst-alpha")->name == "Alpha");

    request.instance_row.name = "Alpha renamed";
    push_command(manager, request);

    REQUIRE(manager.instances().size() == 2);
    REQUIRE(manager.instances().find("inst-alpha")->name == "Alpha renamed");

    manager.stop();
}

TEST_CASE("a remove_instance command takes the instance's identities with it",
    "[runtime]") {
    const temporary_database database("remove-instance");

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());

        command save;
        save.kind = command_kind::save_instance;
        save.instance_row = make_instance("inst-beta", "Beta", "ores.staging.beta");
        save.instance_id = save.instance_row.id;
        push_command(manager, save);

        command save_identity;
        save_identity.kind = command_kind::save_identity;
        save_identity.identity_row = make_identity("ident-1", "inst-beta");
        push_command(manager, save_identity);

        REQUIRE(manager.instances().size() == 2);
        REQUIRE(manager.identities().size() == 1);

        command remove;
        remove.kind = command_kind::remove_instance;
        remove.instance_id = "inst-beta";
        push_command(manager, remove);

        REQUIRE(manager.instances().find("inst-beta") == nullptr);
        REQUIRE(manager.instances().size() == 1);
        REQUIRE(manager.identities().size() == 0);

        manager.stop();
    }

    /* The cascade has to be in the store, not only in the tables. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.instances().size() == 1);
    REQUIRE(reopened.identities().size() == 0);
    reopened.stop();
}

TEST_CASE("a chart window is opened by command and closed by command",
    "[runtime]") {
    const temporary_database database("window");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.windows().size() == 1);

    command open;
    open.kind = command_kind::open_window;
    open.new_window_kind = window_kind::chart;
    push_command(manager, open);

    REQUIRE(manager.windows().size() == 2);
    const auto window = manager.windows().find("w-1");
    REQUIRE(window.has_value());
    REQUIRE(manager.windows().kind(*window) == window_kind::chart);

    const auto& state = manager.windows().state(*window);
    REQUIRE(std::holds_alternative<chart_state>(state));
    REQUIRE(std::get<chart_state>(state).label == "Series");

    command refresh;
    refresh.kind = command_kind::refresh_window;
    refresh.window_id = "w-1";
    push_command(manager, refresh);

    REQUIRE(manager.windows().next_refresh_ns(*window) > 0);

    command close;
    close.kind = command_kind::close_window;
    close.window_id = "w-1";
    push_command(manager, close);

    REQUIRE_FALSE(manager.windows().find("w-1").has_value());
    REQUIRE(manager.windows().size() == 1);

    manager.stop();
}

TEST_CASE("a session_failed event records the sentence a person reads",
    "[runtime]") {
    const temporary_database database("session-failed");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance("inst-refused", "Refused", "ores.test.refused");

    /* Nothing listens on port one, so the worker's own connect is refused. */
    save.instance_row.nats_url = "nats://127.0.0.1:1";
    save.instance_id = save.instance_row.id;
    push_command(manager, save);

    command save_identity;
    save_identity.kind = command_kind::save_identity;
    save_identity.identity_row = make_identity("ident-1", "inst-refused");
    push_command(manager, save_identity);

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = "inst-refused";
    connect.identity_id = "ident-1";
    connect.principal = "alice@example.test";
    connect.password = "not-a-real-secret";
    push_command(manager, connect);

    REQUIRE(manager.sessions().find("inst-refused", "ident-1").has_value());

    /*
     * Let the worker's own attempt land first, so the event below is applied
     * after it and the assertion is about this event rather than about the
     * race between the two.
     */
    const auto settled = wait_until([&manager] {
        manager.pump();
        const auto handle = manager.sessions().find("inst-refused", "ident-1");
        return handle
            && manager.sessions().status(*handle) == session_status::failed;
    }, std::chrono::seconds(10));
    REQUIRE(settled);

    event fact;
    fact.kind = event_kind::session_failed;
    fact.instance_id = "inst-refused";
    fact.identity_id = "ident-1";
    fact.code = "unreachable";
    fact.message = "the instance could not be reached";
    manager.events().push(fact);
    manager.pump();

    const auto handle = manager.sessions().find("inst-refused", "ident-1");
    REQUIRE(handle.has_value());
    REQUIRE(manager.sessions().status(*handle) == session_status::failed);
    REQUIRE(manager.sessions().last_error(*handle) ==
        "the instance could not be reached (unreachable)");

    manager.stop();
}
