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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.runtime/worker/probe_worker.hpp"
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::chart_state;
using hedgr::model::domain::account;
using hedgr::model::domain::secret_string;
using hedgr::model::domain::accounts_root_id_for;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::label_colour;
using hedgr::model::domain::label_severity;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::reachability;
using hedgr::model::domain::readiness;
using hedgr::model::domain::session_status;
using hedgr::model::domain::shortcut;
using hedgr::model::domain::window_kind;
using hedgr::model::domain::market_level;
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
using hedgr::model::state::catalogue_state;
using hedgr::model::state::condition_source;
using hedgr::model::state::window_handle;
using hedgr::runtime::async::executor;
using hedgr::runtime::manager::instance_manager;
using hedgr::runtime::worker::failure_detail;
using hedgr::session::transport::session_errc;
using hedgr::session::transport::session_error;
using hedgr::session::transport::session_options;

/**
 * @brief A token no other process or run is likely to pick.
 *
 * The suite opens real files, so a path derived only from a counter is unique
 * within one process and repeats on the next run. Two runs at once, or a run
 * that was killed before its destructors, would then share a file and a test
 * would inherit state it never created. A value drawn from the machine's
 * entropy makes that collision impossible rather than unlikely.
 */
[[nodiscard]] std::string unique_token() {
    std::random_device source;
    return std::format("{:08x}{:08x}", source(), source());
}

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * Every test gets its own path, because a shared one would make the suite pass
 * or fail on the order the tests happen to run in, and the name carries a
 * per-run token so that a file left behind by a killed run can never be picked
 * up by the next one.
 *
 * The constructor erases the path before handing it out as well as after, so a
 * test begins from a file that does not exist whatever happened last time.
 */
class temporary_database final {
public:
    explicit temporary_database(std::string_view tag)
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.{}.{}.{}.sqlite", tag, unique_token(),
                  next_serial())) {
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
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = std::move(subject_prefix);
    return row;
}

/**
 * @brief The monotonic clock the probe schedule is written against.
 *
 * The same clock the runtime uses, so a deadline the runtime computed can be
 * compared with "now" here rather than with a wall-clock instant that means
 * nothing to either.
 */
[[nodiscard]] std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * @brief The instance row with this id, or nullptr.
 *
 * The table answers a string id with a handle and a handle with a row, so the
 * two steps are spelled once here rather than at every assertion.
 */
[[nodiscard]] const instance* find_instance(const instance_manager& manager,
    std::string_view id) {
    const auto handle = manager.instances().find(id);
    return handle ? manager.instances().get(*handle) : nullptr;
}

/** @brief The shortcuts bar entry with this id, or nullptr. */
[[nodiscard]] const shortcut* find_shortcut(const instance_manager& manager,
    std::string_view id) {
    for (const auto& row : manager.shortcuts().rows()) {
        if (row.id == id)
            return &row;
    }
    return nullptr;
}

/** @brief An account row belonging to @p instance_id. */
[[nodiscard]] account make_account(std::string id, std::string instance_id) {
    account row;
    row.id = std::move(id);
    row.instance_id = std::move(instance_id);
    row.name = "ops read-only";
    row.principal = "alice@example.test";
    return row;
}

void push_command(instance_manager& manager, command request) {
    manager.commands().push(std::move(request));
    manager.pump();
}

/**
 * @brief An instance nothing listens on, added by command.
 *
 * Port one is reserved and nothing binds it, so a connect to it is refused
 * immediately rather than waiting out a timeout. That is the only kind of
 * unreachable deployment a test may depend on: everything else a probe could
 * meet needs a daemon, and a suite that needs a daemon is a suite that fails
 * on a developer's machine for no reason.
 */
void add_unreachable_instance(instance_manager& manager, std::string id,
    std::string name = "Refused") {
    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance(std::move(id), std::move(name),
        "ores.test.refused");
    save.instance_row.nats_url = "nats://127.0.0.1:1";
    save.instance_id = save.instance_row.id;
    push_command(manager, save);
}

/** @brief Runs the worker's sign-in to its refusal, leaving the session failed. */
void refuse_sign_in(instance_manager& manager, const std::string& instance_id,
    const std::string& account_id) {
    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = instance_id;
    connect.account_id = account_id;
    connect.principal = "alice@example.test";
    connect.password = secret_string{std::string{"not-a-real-secret"}};
    push_command(manager, connect);

    const auto refused = wait_until([&] {
        manager.pump();
        const auto handle = manager.sessions().find(instance_id, account_id);
        return handle
            && manager.sessions().status(*handle) == session_status::failed;
    }, std::chrono::seconds(10));
    REQUIRE(refused);
}

/**
 * @brief Signs @p account_id in on an unreachable instance, as far as the
 * frame loop can tell.
 *
 * The worker's own sign-in is refused, and only then is the authenticated fact
 * pushed by hand: pushing it earlier would let the refusal land on top of it.
 * Every read the session starts afterwards connects to the same refused port,
 * so it fails fast and its answer exercises the real event path.
 */
void sign_in_unreachable(instance_manager& manager,
    const std::string& instance_id, const std::string& account_id,
    std::string token = "a-token-nobody-checks", std::string tenant_id = {}) {
    refuse_sign_in(manager, instance_id, account_id);

    event fact;
    fact.kind = event_kind::session_authenticated;
    fact.instance_id = instance_id;
    fact.account_id = account_id;
    fact.tenant_id = std::move(tenant_id);
    fact.token = secret_string{std::move(token)};
    manager.events().push(fact);
    manager.pump();

    const auto handle = manager.sessions().find(instance_id, account_id);
    REQUIRE(handle.has_value());
    REQUIRE(manager.sessions().status(*handle) == session_status::authenticated);
}

[[nodiscard]] command refresh(std::string window_id) {
    command request;
    request.kind = command_kind::refresh_window;
    request.window_id = std::move(window_id);
    return request;
}

/**
 * @brief An open_window request for @p kind on one pair.
 *
 * The series id names a chart's series or a term structure's curve, and a
 * term structure gets two members so the runtime treats it as readable. The
 * CRM fields are set for every kind and ignored by all but the matrix.
 */
[[nodiscard]] command open_window(window_kind kind, std::string instance_id,
    std::string account_id, std::string series_id = {}) {
    command request;
    request.kind = command_kind::open_window;
    request.instance_id = std::move(instance_id);
    request.account_id = std::move(account_id);
    request.new_window_kind = kind;
    request.series_id = std::move(series_id);
    request.series_label = "FX GBP/USD";
    request.member_ids = {"m-1y", "m-2y"};
    request.member_tenors = {"1Y", "2Y"};
    request.party_id = "p";
    request.crm_name = "majors";
    return request;
}

/** @brief The first live window of @p kind, or nothing. */
[[nodiscard]] std::optional<window_handle> find_window_of_kind(
    const instance_manager& manager, window_kind kind) {
    const auto& layout = manager.windows().layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const window_handle window{index, layout.generation[index]};
        if (manager.windows().kind(window) == kind)
            return window;
    }
    return std::nullopt;
}

constexpr std::int64_t parked_deadline = std::numeric_limits<std::int64_t>::max();

/**
 * @brief Saves a wall of four windows against one signed-in pair, then lets
 * the runtime go, so a second runtime on the same store restores the wall
 * with its session key but no session.
 */
void save_wall_signed_in(const temporary_database& database) {
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    add_unreachable_instance(manager, "inst-refused");
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);
    sign_in_unreachable(manager, "inst-refused", "ident-1");

    push_command(manager, open_window(window_kind::series_browser,
        "inst-refused", "ident-1"));
    push_command(manager, open_window(window_kind::command_centre,
        "inst-refused", "ident-1"));
    push_command(manager, open_window(window_kind::chart,
        "inst-refused", "ident-1", "s-1"));
    push_command(manager, open_window(window_kind::term_structure,
        "inst-refused", "ident-1", "curve-1"));
    REQUIRE(manager.windows().size() == 6);
    manager.stop();
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

    /*
     * The rows span includes the holes a removal leaves behind, so the walk
     * checks the slab before reading the one row a fresh store holds.
     */
    const auto& instances = manager.instances();
    const auto& layout = instances.layout();
    REQUIRE(layout.is_alive(0));
    const auto& row = instances.rows()[0];
    REQUIRE(row.name == "local development");
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
    REQUIRE(find_instance(manager, "inst-alpha") != nullptr);
    REQUIRE(find_instance(manager, "inst-alpha")->name == "Alpha");

    request.instance_row.name = "Alpha renamed";
    push_command(manager, request);

    REQUIRE(manager.instances().size() == 2);
    REQUIRE(find_instance(manager, "inst-alpha")->name == "Alpha renamed");

    manager.stop();
}

TEST_CASE("a remove_instance command takes the instance's accounts with it",
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

        command save_account;
        save_account.kind = command_kind::save_account;
        save_account.account_row = make_account("ident-1", "inst-beta");
        push_command(manager, save_account);

        REQUIRE(manager.instances().size() == 2);
        /*
         * The instance the start seeded already carries its two logins, so the
         * account this case wrote for inst-beta makes three. Seeding runs at
         * start, so it has not reached an instance written afterwards.
         */
        REQUIRE(manager.accounts().size() == 3);
        REQUIRE(manager.accounts().find("ident-1").has_value());

        command remove;
        remove.kind = command_kind::remove_instance;
        remove.instance_id = "inst-beta";
        push_command(manager, remove);

        REQUIRE(find_instance(manager, "inst-beta") == nullptr);
        REQUIRE(manager.instances().size() == 1);
        /* Only the seeded logins are left, and inst-beta's account is gone. */
        REQUIRE(manager.accounts().size() == 2);
        REQUIRE_FALSE(manager.accounts().find("ident-1").has_value());

        manager.stop();
    }

    /* The cascade has to be in the store, not only in the tables. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.instances().size() == 1);
    REQUIRE(reopened.accounts().size() == 2);
    REQUIRE_FALSE(reopened.accounts().find("ident-1").has_value());
    reopened.stop();
}

TEST_CASE("start creates both roots and is idempotent", "[runtime]") {
    const temporary_database database("roots");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /* One instance was seeded, so there is one tree per tree kind plus the
     * Privileged folder its own logins sit in. */
    REQUIRE(manager.instances().size() == 1);
    REQUIRE(manager.folders().size() == 3);

    const auto instances_root =
        manager.folders().root(folder_tree::instances, "");
    REQUIRE(instances_root.has_value());
    REQUIRE(manager.folders().get(*instances_root)->id == "instances-root");
    REQUIRE(manager.folders().get(*instances_root)->name == "Instances");
    REQUIRE(manager.folders().get(*instances_root)->parent_id == "");

    const auto& instance_row = manager.instances().rows()[0];
    const auto accounts_root =
        manager.folders().root(folder_tree::accounts, instance_row.id);
    REQUIRE(accounts_root.has_value());
    REQUIRE(manager.folders().get(*accounts_root)->id ==
        "accounts-root:" + instance_row.id);
    REQUIRE(manager.folders().get(*accounts_root)->owner_id == instance_row.id);
    REQUIRE(manager.folders().get(*accounts_root)->name == "Accounts");

    /* A second start converges rather than creating a second pair of roots. */
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.folders().size() == 3);

    manager.stop();

    /* Both roots are in the store, not only in the table. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.folders().size() == 3);
    REQUIRE(reopened.folders().root(folder_tree::instances, "").has_value());
    REQUIRE(reopened.folders().root(folder_tree::accounts,
        reopened.instances().rows()[0].id).has_value());
    reopened.stop();
}

TEST_CASE("start gives an already-stored instance its accounts root",
    "[runtime]") {
    const temporary_database database("missing-root");

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());

        /* A second instance is stored with its root, as a normal save does. */
        command save;
        save.kind = command_kind::save_instance;
        save.instance_row =
            make_instance("inst-epsilon", "Epsilon", "ores.staging.epsilon");
        save.instance_id = save.instance_row.id;
        push_command(manager, save);

        REQUIRE(manager.folders().size() == 4);
        manager.stop();
    }

    /*
     * A second manager over the same file finds an instance that already has a
     * root and must not create a second one. This is the same convergence the
     * first start relies on, reached from a store rather than from memory.
     */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.folders().size() == 5);
    REQUIRE(reopened.folders().root(folder_tree::accounts, "inst-epsilon")
                .has_value());
    reopened.stop();
}

TEST_CASE("adding an instance creates its accounts root", "[runtime]") {
    const temporary_database database("instance-root");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.folders().size() == 3);

    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance("inst-gamma", "Gamma", "ores.staging.gamma");
    save.instance_id = save.instance_row.id;
    push_command(manager, save);

    REQUIRE(manager.folders().size() == 4);

    const auto root = manager.folders().find("accounts-root:inst-gamma");
    REQUIRE(root.has_value());
    REQUIRE(manager.folders().get(*root)->parent_id == "");
    REQUIRE(manager.folders().get(*root)->owner_id == "inst-gamma");
    REQUIRE(manager.folders().get(*root)->tree == folder_tree::accounts);

    /* Saving the same instance again does not add a second root. */
    push_command(manager, save);
    REQUIRE(manager.folders().size() == 4);

    manager.stop();
}

TEST_CASE("removing an instance removes its accounts folders", "[runtime]") {
    const temporary_database database("remove-folders");

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());

        command save;
        save.kind = command_kind::save_instance;
        save.instance_row = make_instance("inst-delta", "Delta", "ores.staging.delta");
        save.instance_id = save.instance_row.id;
        push_command(manager, save);

        command add_folder;
        add_folder.kind = command_kind::save_folder;
        add_folder.folder_row.id = "fold-delta";
        add_folder.folder_row.parent_id = "accounts-root:inst-delta";
        add_folder.folder_row.name = "Production";
        push_command(manager, add_folder);

        command add_child;
        add_child.kind = command_kind::save_folder;
        add_child.folder_row.id = "fold-delta-nested";
        add_child.folder_row.parent_id = "fold-delta";
        add_child.folder_row.name = "Nested";
        push_command(manager, add_child);

        REQUIRE(manager.folders().size() == 6);

        command remove;
        remove.kind = command_kind::remove_instance;
        remove.instance_id = "inst-delta";
        push_command(manager, remove);

        /* The instance, its root and both folders below it are gone. */
        REQUIRE(manager.folders().size() == 3);
        REQUIRE_FALSE(manager.folders().find("accounts-root:inst-delta")
                          .has_value());
        REQUIRE_FALSE(manager.folders().find("fold-delta").has_value());
        REQUIRE_FALSE(manager.folders().find("fold-delta-nested").has_value());

        manager.stop();
    }

    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.folders().size() == 3);
    reopened.stop();
}

TEST_CASE("a root folder cannot be removed or re-parented", "[runtime]") {
    const temporary_database database("root-refused");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /* The one instance the fresh store was seeded with owns the second root. */
    const auto accounts_root =
        accounts_root_id_for(manager.instances().rows()[0].id);

    command remove;
    remove.kind = command_kind::remove_folder;
    remove.folder_row.id = "instances-root";
    push_command(manager, remove);

    REQUIRE(manager.folders().size() == 3);
    REQUIRE(manager.folders().find("instances-root").has_value());
    REQUIRE(manager.last_error() ==
        "cannot remove folder instances-root: it is the root of the instances "
        "tree");

    command move;
    move.kind = command_kind::save_folder;
    move.folder_row.id = "instances-root";
    move.folder_row.parent_id = accounts_root;
    move.folder_row.name = "Instances";
    push_command(manager, move);

    REQUIRE(manager.folders().get(*manager.folders().find("instances-root"))
                ->parent_id == "");
    REQUIRE(manager.last_error() ==
        "cannot re-parent folder instances-root: it is the root of the "
        "instances tree");

    /* An accounts root is refused for the same reason. */
    command remove_accounts;
    remove_accounts.kind = command_kind::remove_folder;
    remove_accounts.folder_row.id = accounts_root;
    push_command(manager, remove_accounts);

    REQUIRE(manager.folders().size() == 3);
    REQUIRE(manager.last_error() ==
        "cannot remove folder " + accounts_root
        + ": it is a root of its tree");

    manager.stop();
}

TEST_CASE("a folder is refused when it would contain itself", "[runtime]") {
    const temporary_database database("folder-cycle");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    command parent;
    parent.kind = command_kind::save_folder;
    parent.folder_row.id = "fold-a";
    parent.folder_row.parent_id = "instances-root";
    parent.folder_row.name = "A";
    push_command(manager, parent);

    command child;
    child.kind = command_kind::save_folder;
    child.folder_row.id = "fold-b";
    child.folder_row.parent_id = "fold-a";
    child.folder_row.name = "B";
    push_command(manager, child);

    /* A is made a child of its own child, which would close the loop. */
    command cyclic;
    cyclic.kind = command_kind::save_folder;
    cyclic.folder_row.id = "fold-a";
    cyclic.folder_row.parent_id = "fold-b";
    cyclic.folder_row.name = "A";
    push_command(manager, cyclic);

    REQUIRE(manager.folders().get(*manager.folders().find("fold-a"))->parent_id
        == "instances-root");
    REQUIRE(manager.last_error() ==
        "cannot parent folder fold-a to fold-b: it would contain itself");

    /* Removing the parent takes the whole branch with it. */
    command remove;
    remove.kind = command_kind::remove_folder;
    remove.folder_row.id = "fold-a";
    push_command(manager, remove);

    REQUIRE(manager.folders().size() == 3);
    REQUIRE_FALSE(manager.folders().find("fold-b").has_value());

    manager.stop();
}

TEST_CASE("a label is attached, listed and detached by command", "[runtime]") {
    const temporary_database database("labels");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    command save;
    save.kind = command_kind::save_label;
    save.label_row.id = "lbl-prod";
    save.label_row.name = "production";
    save.label_row.colour = label_colour::red;
    save.label_row.severity = label_severity::danger;
    push_command(manager, save);

    REQUIRE(manager.labels().size() == 1);
    REQUIRE(manager.labels().get(*manager.labels().find("lbl-prod"))->severity
        == label_severity::danger);

    const auto& instance_row = manager.instances().rows()[0];
    command attach;
    attach.kind = command_kind::attach_label;
    attach.labelling_row.owner = labelling_owner::instance;
    attach.labelling_row.owner_id = instance_row.id;
    attach.labelling_row.label_id = "lbl-prod";
    push_command(manager, attach);

    REQUIRE(manager.labellings().size() == 1);

    /* Attaching the same pair again changes nothing. */
    push_command(manager, attach);
    REQUIRE(manager.labellings().size() == 1);

    const auto attached = manager.labellings().labels_of(
        labelling_owner::instance, instance_row.id);
    REQUIRE(attached.size() == 1);

    /* An attachment to a row that does not exist is refused. */
    command dangling = attach;
    dangling.labelling_row.owner_id = "inst-missing";
    push_command(manager, dangling);
    REQUIRE(manager.labellings().size() == 1);
    REQUIRE(manager.last_error() ==
        "cannot attach a label: no instance inst-missing");

    command detach;
    detach.kind = command_kind::detach_label;
    detach.labelling_row = attach.labelling_row;
    push_command(manager, detach);

    REQUIRE(manager.labellings().size() == 0);

    manager.stop();

    /* The label is in the store even though its attachment was removed. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.labels().size() == 1);
    REQUIRE(reopened.labellings().size() == 0);
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

    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = "inst-refused";
    connect.account_id = "ident-1";
    connect.principal = "alice@example.test";
    connect.password = secret_string{std::string{"not-a-real-secret"}};
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
    fact.account_id = "ident-1";
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

TEST_CASE("an instance whose certificate cannot be opened names the file",
    "[runtime]") {
    const temporary_database database("tls-file");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /*
     * A relative path, so this covers the whole line rather than one link of
     * it: the row stores what the operator typed, the runtime resolves it
     * against the store's folder, and the transport reports the file it could
     * not open. Asserting on the resolved path is what proves the middle step
     * happened -- a bare "keys/nats/ca.crt" would also match an unresolved
     * pass-through.
     */
    const auto resolved =
        database.path().parent_path() / "keys/nats/ca.crt";

    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance("inst-tls", "Wrong path", "ores.test.tls");
    save.instance_row.tls_ca = "keys/nats/ca.crt";
    save.instance_id = save.instance_row.id;
    push_command(manager, save);

    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-tls", "inst-tls");
    push_command(manager, save_account);

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = "inst-tls";
    connect.account_id = "ident-tls";
    connect.principal = "alice@example.test";
    connect.password = secret_string{std::string{"not-a-real-secret"}};
    push_command(manager, connect);

    const auto settled = wait_until([&manager] {
        manager.pump();
        const auto handle = manager.sessions().find("inst-tls", "ident-tls");
        return handle
            && manager.sessions().status(*handle) == session_status::failed;
    }, std::chrono::seconds(10));
    REQUIRE(settled);

    const auto handle = manager.sessions().find("inst-tls", "ident-tls");
    REQUIRE(handle.has_value());
    const auto& reported = manager.sessions().last_error(*handle);

    /*
     * A sentence naming the file, not "the instance is not reachable": a
     * mistyped path and a broker that is down look identical otherwise, and
     * they need very different fixes.
     */
    REQUIRE(reported.contains(resolved.string()));
    REQUIRE_FALSE(reported.contains("not reachable"));

    manager.stop();
}

/*
 * The probe tests below use exactly one kind of unreachable deployment: a port
 * nothing listens on. A probe of a live broker is not tested here on purpose.
 * It would need the broker, its subject prefix and -- for readiness -- an IAM
 * service, so the suite would pass or fail on whether a daemon happens to be
 * running; the answered round trip is covered by running the application
 * against a real deployment. What is covered here is the part that has no
 * daemon in it: the translation of a refused connect, the isolation from the
 * session table, and the schedule the answer feeds.
 */
TEST_CASE("a probe against a refused port reports unreachable", "[runtime]") {
    const temporary_database database("probe-unreachable");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");

    const auto handle = manager.instances().find("inst-refused");
    REQUIRE(handle.has_value());

    /*
     * Bounded, so a probe that never reports fails the test rather than
     * hanging the suite on a worker that never ran.
     */
    const auto settled = wait_until([&manager, &handle] {
        manager.pump();
        return manager.probes().get(*handle) != nullptr;
    }, std::chrono::seconds(10));
    REQUIRE(settled);

    const auto* result = manager.probes().get(*handle);
    REQUIRE(result != nullptr);
    REQUIRE(result->reach == reachability::unreachable);
    REQUIRE(result->ready == readiness::unknown);
    REQUIRE_FALSE(result->detail.empty());
    REQUIRE(result->observed_at_ns > 0);

    manager.stop();
}

/*
 * The probe asks one question, and a deployment that still needs bootstrapping
 * answers it. A read that nothing answered is therefore a service that is not
 * there, and the pane says which thing is down before it quotes the wire. The
 * two parts are separate lines, because the short answer is the one a reader
 * stops at and the raw text is the one that goes into a ticket.
 */
TEST_CASE("an unanswered setup read is explained rather than quoted",
    "[runtime]") {
    const session_error unanswered{
        .code = session_errc::services_unavailable,
        .message = "the instance is not responding; contact your system "
                   "administrator",
        .detail = "NATS request failed: no service is handling subject "
                  "'ores.dev.brave_hopper.iam.v1.ops.bootstrap_status'"};

    const auto detail = failure_detail(unanswered);

    REQUIRE(detail ==
        "IAM is down\n"
        "NATS request failed: no service is handling subject "
        "'ores.dev.brave_hopper.iam.v1.ops.bootstrap_status'");
    REQUIRE_FALSE(detail.starts_with("NATS"));
    REQUIRE(detail.contains("iam.v1.ops.bootstrap_status"));
}

TEST_CASE("a broker that refused the connect reads as NATS being down",
    "[runtime]") {
    const session_error refused{
        .code = session_errc::unreachable,
        .message = "the instance is not reachable",
        .detail = "NATS connect failed: No servers available"};

    REQUIRE(failure_detail(refused) ==
        "NATS is down\nNATS connect failed: No servers available");
}

TEST_CASE("a failure that is neither fault keeps its own short headline",
    "[runtime]") {
    const session_error timed_out{
        .code = session_errc::timeout,
        .message = "the instance did not answer in time",
        .detail = "NATS request timed out"};

    REQUIRE(failure_detail(timed_out) ==
        "the instance did not answer in time\nNATS request timed out");
}

TEST_CASE("a probe leaves the session table untouched", "[runtime]") {
    const temporary_database database("probe-isolation");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");

    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = "inst-refused";
    connect.account_id = "ident-1";
    connect.principal = "alice@example.test";
    connect.password = secret_string{std::string{"not-a-real-secret"}};
    push_command(manager, connect);

    const auto handle = manager.instances().find("inst-refused");
    REQUIRE(handle.has_value());
    const auto session = manager.sessions().find("inst-refused", "ident-1");
    REQUIRE(session.has_value());
    REQUIRE(manager.sessions().size() == 1);

    /*
     * The session's own attempt is left to settle first, so what the probe is
     * measured against is a session that has stopped moving and any change to
     * it can only be the probe's doing.
     */
    const auto session_settled = wait_until([&manager, &session] {
        manager.pump();
        return manager.sessions().status(*session) == session_status::failed;
    }, std::chrono::seconds(10));
    REQUIRE(session_settled);

    const auto status_before = manager.sessions().status(*session);
    REQUIRE(status_before == session_status::failed);

    const auto probed = wait_until([&manager, &handle] {
        manager.pump();
        return manager.probes().get(*handle) != nullptr;
    }, std::chrono::seconds(10));
    REQUIRE(probed);

    /*
     * The claim ADR 5 rests on, stated as a count and a status: a probe that
     * ran to completion next to a session added no session, removed none, and
     * did not move the one that was there.
     */
    REQUIRE(manager.sessions().size() == 1);
    REQUIRE(manager.sessions().status(*session) == status_before);
    REQUIRE(manager.sessions().last_error(*session) ==
        "the instance is not reachable (unreachable)");

    manager.stop();
}

TEST_CASE("a failed probe backs off and an answered one returns to the cadence",
    "[runtime]") {
    const temporary_database database("probe-backoff");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");

    const auto handle = manager.instances().find("inst-refused");
    REQUIRE(handle.has_value());

    const auto first = wait_until([&manager, &handle] {
        manager.pump();
        return manager.probes().get(*handle) != nullptr;
    }, std::chrono::seconds(10));
    REQUIRE(first);

    /*
     * The first failure waits ten seconds: the five-second cadence doubled.
     * The bounds are literal and a second wide, because the deadline is
     * written from the runtime's own clock a moment before this reads it.
     */
    const auto first_deadline = manager.probes().next_probe_ns(*handle);
    const auto after_first = steady_now_ns();
    REQUIRE(first_deadline - after_first > 9'000'000'000);
    REQUIRE(first_deadline - after_first <= 10'000'000'000);

    /*
     * A second attempt is forced rather than waited for: the backoff is
     * ten seconds long and a suite that sleeps through its own schedules is
     * a suite nobody runs.
     */
    const auto first_observed = manager.probes().get(*handle)->observed_at_ns;
    command probe;
    probe.kind = command_kind::probe_instance;
    probe.instance_id = "inst-refused";
    push_command(manager, probe);

    const auto second = wait_until([&manager, &handle, first_observed] {
        manager.pump();
        const auto* result = manager.probes().get(*handle);
        return result != nullptr && result->observed_at_ns != first_observed;
    }, std::chrono::seconds(10));
    REQUIRE(second);

    const auto second_deadline = manager.probes().next_probe_ns(*handle);
    const auto after_second = steady_now_ns();
    REQUIRE(second_deadline - after_second > 19'000'000'000);
    REQUIRE(second_deadline - after_second <= 20'000'000'000);

    /*
     * An answered probe returns to the five-second cadence. The answer is
     * synthesised rather than fetched, because a real one needs a live broker
     * and this asserts the schedule's response to an answer, not the wire. The
     * structured fields travel with the flag, which is what separates this from
     * the refused probes above: they carry no answer and only a sentence.
     */
    event fact;
    fact.kind = event_kind::probe_result;
    fact.instance_id = "inst-refused";
    fact.reach = reachability::reachable;
    fact.ready = readiness::ready;
    fact.bootstrap_answered = true;
    fact.bootstrap.in_bootstrap_mode = false;
    fact.bootstrap.has_tenant = true;
    fact.bootstrap.version = "capture build";
    manager.events().push(fact);
    manager.pump();

    const auto* answered = manager.probes().get(*handle);
    REQUIRE(answered != nullptr);
    REQUIRE(answered->reach == reachability::reachable);
    REQUIRE(answered->ready == readiness::ready);
    REQUIRE(answered->bootstrap.has_value());
    REQUIRE(answered->bootstrap->in_bootstrap_mode == false);
    REQUIRE(answered->bootstrap->has_tenant == true);
    REQUIRE(answered->bootstrap->version == "capture build");

    const auto reset_deadline = manager.probes().next_probe_ns(*handle);
    const auto after_reset = steady_now_ns();
    REQUIRE(reset_deadline - after_reset > 4'000'000'000);
    REQUIRE(reset_deadline - after_reset <= 5'000'000'000);

    manager.stop();
}

TEST_CASE("a probe result for a removed instance is dropped", "[runtime]") {
    const temporary_database database("probe-removed");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-gone");
    const auto gone = manager.instances().find("inst-gone");
    REQUIRE(gone.has_value());

    command remove;
    remove.kind = command_kind::remove_instance;
    remove.instance_id = "inst-gone";
    push_command(manager, remove);
    REQUIRE_FALSE(manager.instances().find("inst-gone").has_value());

    /*
     * The next row takes the freed slot, so a result applied by slot rather
     * than by id would land on it.
     */
    add_unreachable_instance(manager, "inst-next", "Refused next");
    const auto next = manager.instances().find("inst-next");
    REQUIRE(next.has_value());

    event fact;
    fact.kind = event_kind::probe_result;
    fact.instance_id = "inst-gone";
    fact.reach = reachability::reachable;
    fact.ready = readiness::ready;
    fact.detail = "answer for the removed instance";
    manager.events().push(fact);
    manager.pump();

    REQUIRE(manager.probes().get(*gone) == nullptr);
    const auto* result = manager.probes().get(*next);
    REQUIRE((result == nullptr || result->detail != fact.detail));

    manager.stop();
}

TEST_CASE("probe_all moves every deadline and never doubles a probe in flight",
    "[runtime]") {
    const temporary_database database("probe-all");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-a");
    add_unreachable_instance(manager, "inst-b", "Refused too");
    const auto a = manager.instances().find("inst-a");
    const auto b = manager.instances().find("inst-b");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    const auto first = wait_until([&] {
        manager.pump();
        return manager.probes().get(*a) != nullptr
            && manager.probes().get(*b) != nullptr;
    }, std::chrono::seconds(10));
    REQUIRE(first);
    const auto a_observed = manager.probes().get(*a)->observed_at_ns;
    const auto b_observed = manager.probes().get(*b)->observed_at_ns;
    REQUIRE(manager.probes().next_probe_ns(*a) > steady_now_ns());
    REQUIRE(manager.probes().next_probe_ns(*b) > steady_now_ns());

    command all;
    all.kind = command_kind::probe_all;
    push_command(manager, all);

    /*
     * The sweep spawned both probes in the same pump, and a deadline stays
     * where probe_all put it until the answer moves it.
     */
    REQUIRE(manager.probes().next_probe_ns(*a) == 0);
    REQUIRE(manager.probes().next_probe_ns(*b) == 0);

    /*
     * Pumped again at once, with both deadlines still due: only the in-flight
     * entries stop a second probe of each.
     */
    for (int pass = 0; pass < 50; ++pass)
        manager.pump();

    const auto second = wait_until([&] {
        manager.pump();
        return manager.probes().get(*a)->observed_at_ns != a_observed
            && manager.probes().get(*b)->observed_at_ns != b_observed;
    }, std::chrono::seconds(10));
    REQUIRE(second);

    /*
     * A duplicate answer would be a third failure and push the deadline out to
     * forty seconds. Given time to arrive, it must not have.
     */
    const auto settle_until = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < settle_until) {
        manager.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto now = steady_now_ns();
    REQUIRE(manager.probes().next_probe_ns(*a) - now > 15'000'000'000);
    REQUIRE(manager.probes().next_probe_ns(*a) - now <= 20'000'000'000);
    REQUIRE(manager.probes().next_probe_ns(*b) - now > 15'000'000'000);
    REQUIRE(manager.probes().next_probe_ns(*b) - now <= 20'000'000'000);

    manager.stop();
}

TEST_CASE("a change of reachability raises one named signal", "[runtime]") {
    const temporary_database database("probe-signal");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    static_cast<void>(hedgr::model::signal::bus().drain());
    add_unreachable_instance(manager, "inst-signal", "Signalled");
    const auto handle = manager.instances().find("inst-signal");
    REQUIRE(handle.has_value());

    const auto first = wait_until([&] {
        manager.pump();
        return manager.probes().get(*handle) != nullptr;
    }, std::chrono::seconds(10));
    REQUIRE(first);

    /*
     * The bus is the process's, and the seeded instance is probed too, so only
     * the signals that name this instance are counted. A first result has
     * nothing to differ from and raises nothing.
     */
    const auto about_this = [] {
        std::vector<hedgr::model::signal::signal> result;
        for (auto& value : hedgr::model::signal::bus().drain()) {
            if (value.detail.starts_with("Signalled "))
                result.push_back(std::move(value));
        }
        return result;
    };
    REQUIRE(about_this().empty());

    /*
     * Readiness moved as well, and only reachability is announced: one fact,
     * one signal.
     */
    event restored;
    restored.kind = event_kind::probe_result;
    restored.instance_id = "inst-signal";
    restored.reach = reachability::reachable;
    restored.ready = readiness::ready;
    manager.events().push(restored);
    manager.pump();

    const auto up = about_this();
    REQUIRE(up.size() == 1);
    REQUIRE(up.front().name ==
        hedgr::model::signal::names::instance_reachability_restored);
    REQUIRE(up.front().detail == "Signalled answers its broker again");

    event lost;
    lost.kind = event_kind::probe_result;
    lost.instance_id = "inst-signal";
    lost.reach = reachability::unreachable;
    manager.events().push(lost);
    manager.pump();

    const auto down = about_this();
    REQUIRE(down.size() == 1);
    REQUIRE(down.front().name ==
        hedgr::model::signal::names::instance_reachability_lost);
    REQUIRE(down.front().detail == "Signalled no longer answers its broker");

    manager.stop();
}

TEST_CASE("removing a folder re-homes what is under it", "[runtime]") {
    const temporary_database database("folder-rehome");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    const auto seeded = *manager.instances().rows().begin();

    command parent;
    parent.kind = command_kind::save_folder;
    parent.folder_row.id = "fold-doomed";
    parent.folder_row.parent_id = "instances-root";
    parent.folder_row.name = "Doomed";
    push_command(manager, parent);

    /* The seeded instance is moved into the folder that is about to go. */
    command move;
    move.kind = command_kind::save_instance;
    move.instance_row = *manager.instances().get(*manager.instances().find(seeded.id));
    move.instance_row.folder_id = "fold-doomed";
    push_command(manager, move);
    REQUIRE(manager.instances().get(*manager.instances().find(seeded.id))->folder_id
        == "fold-doomed");

    command remove;
    remove.kind = command_kind::remove_folder;
    remove.folder_row.id = "fold-doomed";
    push_command(manager, remove);

    /*
     * The folder is gone and the instance is not: removing a folder removes the
     * folder, and what was under it moves to the root rather than being
     * deleted. A row naming a folder that no longer exists would be a tree with
     * a branch that leads nowhere.
     */
    REQUIRE_FALSE(manager.folders().find("fold-doomed").has_value());
    REQUIRE(manager.instances().get(*manager.instances().find(seeded.id))->folder_id
        == "instances-root");
    REQUIRE(manager.last_error().empty());
}

TEST_CASE("a second request for one accounts window focuses the first",
    "[runtime]") {
    const temporary_database database("accounts-window");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    const auto seeded = *manager.instances().rows().begin();

    const auto first = manager.open_accounts_window(seeded.id);
    const auto second = manager.open_accounts_window(seeded.id);

    /*
     * One accounts window per instance, answered by a derived id rather than by
     * a scan: the second request names the window the first made instead of
     * making another.
     */
    REQUIRE(first == second);
    REQUIRE(manager.windows().size() == 2);

    /* And a workspace is keyed by the pair, so two accounts are two windows. */
    command open;
    open.kind = command_kind::open_window;
    open.instance_id = seeded.id;
    open.account_id = "acct-one";
    open.new_window_kind = window_kind::workspace;
    push_command(manager, open);

    command twice;
    twice.kind = command_kind::open_window;
    twice.instance_id = seeded.id;
    twice.account_id = "acct-one";
    twice.new_window_kind = window_kind::workspace;
    push_command(manager, twice);

    REQUIRE(manager.windows().size() == 3);

    command other;
    other.kind = command_kind::open_window;
    other.instance_id = seeded.id;
    other.account_id = "acct-two";
    other.new_window_kind = window_kind::workspace;
    push_command(manager, other);

    REQUIRE(manager.windows().size() == 4);
}

TEST_CASE("an accounts window reopens rather than being made again",
    "[runtime]") {
    const temporary_database database("accounts-reopen");

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    const auto seeded = *manager.instances().rows().begin();

    const auto id = manager.open_accounts_window(seeded.id);
    const auto handle = manager.windows().find(id);
    REQUIRE(handle.has_value());

    command close;
    close.kind = command_kind::close_window;
    close.window_id = id;
    push_command(manager, close);
    REQUIRE_FALSE(manager.windows().find(id).has_value());

    /*
     * Closed is gone: the table removes the row, so the next request makes a
     * window with the same derived id rather than reopening a row that is not
     * there.
     */
    const auto again = manager.open_accounts_window(seeded.id);
    REQUIRE(again == id);
    REQUIRE(manager.windows().find(again).has_value());
}

TEST_CASE("a shortcut is saved, ordered and removed by command", "[runtime]") {
    const temporary_database database("shortcuts");
    std::string seeded_id;

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(manager.shortcuts().size() == 0);
        seeded_id = manager.instances().rows()[0].id;

        const auto save = [&manager](std::string id, window_kind kind,
                                     std::string target, std::int64_t position) {
            command request;
            request.kind = command_kind::save_shortcut;
            request.shortcut_row.id = std::move(id);
            request.shortcut_row.target_kind = kind;
            request.shortcut_row.target_id = std::move(target);
            request.shortcut_row.position = position;
            push_command(manager, std::move(request));
        };

        /*
         * Saved out of order on purpose. The table holds them by position, and
         * a bar that came back in the order it was written rather than the
         * order the operator arranged would be the defect this catches.
         */
        save("sc-manager", window_kind::instance_manager, "", 30);
        save("sc-instance", window_kind::accounts, seeded_id, 10);
        save("sc-grid", window_kind::grid, "", 20);

        REQUIRE(manager.shortcuts().size() == 3);
        REQUIRE(manager.shortcuts().rows()[0].id == "sc-instance");
        REQUIRE(manager.shortcuts().rows()[1].id == "sc-grid");
        REQUIRE(manager.shortcuts().rows()[2].id == "sc-manager");

        command remove;
        remove.kind = command_kind::remove_shortcut;
        remove.shortcut_row.id = "sc-grid";
        push_command(manager, remove);

        REQUIRE(manager.shortcuts().size() == 2);
        REQUIRE(find_shortcut(manager, "sc-grid") == nullptr);
        REQUIRE(manager.last_error().empty());

        manager.stop();
    }

    /* The bar is read back from the store in the order it was left in. */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.shortcuts().size() == 2);

    const auto& first = reopened.shortcuts().rows()[0];
    REQUIRE(first.id == "sc-instance");
    REQUIRE(first.target_kind == window_kind::accounts);
    REQUIRE(first.target_id == seeded_id);
    REQUIRE(first.position == 10);

    const auto& second = reopened.shortcuts().rows()[1];
    REQUIRE(second.id == "sc-manager");
    REQUIRE(second.target_kind == window_kind::instance_manager);
    REQUIRE(second.target_id.empty());
    reopened.stop();
}

TEST_CASE("removing a target takes its shortcuts with it", "[runtime]") {
    const temporary_database database("shortcut-cascade");
    std::string seeded_id;

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        seeded_id = manager.instances().rows()[0].id;

        command save_account;
        save_account.kind = command_kind::save_account;
        save_account.account_row = make_account("ident-sc", seeded_id);
        push_command(manager, save_account);

        const auto save = [&manager](std::string id, window_kind kind,
                                     std::string target, std::int64_t position) {
            command request;
            request.kind = command_kind::save_shortcut;
            request.shortcut_row.id = std::move(id);
            request.shortcut_row.target_kind = kind;
            request.shortcut_row.target_id = std::move(target);
            request.shortcut_row.position = position;
            push_command(manager, std::move(request));
        };

        save("sc-accounts", window_kind::accounts, seeded_id, 10);
        save("sc-workspace", window_kind::workspace, "ident-sc", 20);
        /* A target with no row behind it, which no removal may take. */
        save("sc-manager", window_kind::instance_manager, "", 30);
        REQUIRE(manager.shortcuts().size() == 3);

        /*
         * Removing the account drops the entry that opens its workspace and
         * leaves the entry that opens the instance's accounts alone, because
         * the match is on both halves of the target.
         */
        command remove_account;
        remove_account.kind = command_kind::remove_account;
        remove_account.account_id = "ident-sc";
        push_command(manager, remove_account);

        REQUIRE(manager.shortcuts().size() == 2);
        REQUIRE(find_shortcut(manager, "sc-workspace") == nullptr);
        REQUIRE(find_shortcut(manager, "sc-accounts") != nullptr);
        REQUIRE(manager.last_error().empty());

        /* Removing the instance takes the entry that opened its accounts. */
        command remove_instance;
        remove_instance.kind = command_kind::remove_instance;
        remove_instance.instance_id = seeded_id;
        push_command(manager, remove_instance);

        REQUIRE(manager.shortcuts().size() == 1);
        REQUIRE(find_shortcut(manager, "sc-accounts") == nullptr);
        REQUIRE(find_shortcut(manager, "sc-manager") != nullptr);
        REQUIRE(manager.last_error().empty());

        manager.stop();
    }

    /*
     * The cascade is in the store and not only in the table, so the entries do
     * not come back when the process does.
     */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.shortcuts().size() == 1);
    REQUIRE(reopened.shortcuts().rows()[0].id == "sc-manager");
    reopened.stop();
}

TEST_CASE("two sessions on two instances open their own windows",
    "[windows]") {
    const temporary_database database("multi-session");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());

    const std::string local = manager.instances().rows()[0].id;
    const std::string other = "inst-other";

    command add_instance;
    add_instance.kind = command_kind::save_instance;
    add_instance.instance_id = other;
    add_instance.instance_row = make_instance(other, "other deployment",
        "ores.dev.other");
    push_command(manager, add_instance);

    const auto add_account = [&](std::string id, std::string instance_id) {
        command request;
        request.kind = command_kind::save_account;
        request.account_id = id;
        request.account_row = make_account(id, instance_id);
        push_command(manager, request);
    };
    add_account("acct-a", local);
    add_account("acct-b", other);

    const auto push_window = [&](std::string instance_id, std::string account_id,
                                 window_kind kind, std::string series_id) {
        command request;
        request.kind = command_kind::open_window;
        request.instance_id = std::move(instance_id);
        request.account_id = std::move(account_id);
        request.new_window_kind = kind;
        request.series_id = std::move(series_id);
        request.series_label = "FX GBP/USD";
        push_command(manager, request);
    };

    for (const auto& pair : {std::pair{local, std::string{"acct-a"}},
             std::pair{other, std::string{"acct-b"}}}) {
        push_window(pair.first, pair.second, window_kind::workspace, {});
        push_window(pair.first, pair.second, window_kind::command_centre, {});
        push_window(pair.first, pair.second, window_kind::series_browser, {});
        push_window(pair.first, pair.second, window_kind::chart, "s-1");
    }

    /*
     * The same series id on two deployments is two charts, because the window's
     * identity includes the session. Every surface is one per pair.
     */
    REQUIRE(manager.windows().find("workspace:" + local + ":acct-a").has_value());
    REQUIRE(manager.windows().find("workspace:" + other + ":acct-b").has_value());
    REQUIRE(manager.windows()
            .find("command-centre:" + local + ":acct-a").has_value());
    REQUIRE(manager.windows()
            .find("command-centre:" + other + ":acct-b").has_value());
    REQUIRE(manager.windows()
            .find("series-browser:" + local + ":acct-a").has_value());
    REQUIRE(manager.windows()
            .find("series-browser:" + other + ":acct-b").has_value());
    REQUIRE(manager.windows()
            .find("chart:" + local + ":acct-a:s-1").has_value());
    REQUIRE(manager.windows()
            .find("chart:" + other + ":acct-b:s-1").has_value());

    manager.stop();
}

TEST_CASE("a failed catalogue read leaves the session signed in",
    "[runtime]") {
    const temporary_database database("market-read-failed");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);
    sign_in_unreachable(manager, "inst-refused", "ident-1");

    command open;
    open.kind = command_kind::open_window;
    open.instance_id = "inst-refused";
    open.account_id = "ident-1";
    open.new_window_kind = window_kind::series_browser;
    push_command(manager, open);
    push_command(manager, refresh("series-browser:inst-refused:ident-1"));

    const auto answered = wait_until([&manager] {
        manager.pump();
        const auto* catalogue =
            manager.market().catalogue("inst-refused", "ident-1");
        return catalogue != nullptr
            && catalogue->state == catalogue_state::failed;
    }, std::chrono::seconds(10));
    REQUIRE(answered);
    REQUIRE_FALSE(
        manager.market().catalogue("inst-refused", "ident-1")->error.empty());

    const auto handle = manager.sessions().find("inst-refused", "ident-1");
    REQUIRE(handle.has_value());
    REQUIRE(manager.sessions().status(*handle) == session_status::authenticated);

    manager.stop();
}

TEST_CASE("a read in flight at sign-out does not refuse reads after sign-in",
    "[runtime]") {
    const temporary_database database("read-across-sign-out");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);
    sign_in_unreachable(manager, "inst-refused", "ident-1");

    command open;
    open.kind = command_kind::open_window;
    open.instance_id = "inst-refused";
    open.account_id = "ident-1";
    open.new_window_kind = window_kind::series_browser;
    push_command(manager, open);

    /*
     * One pump applies both commands before any event, so the read's answer
     * can only arrive after the session row is gone.
     */
    command disconnect;
    disconnect.kind = command_kind::disconnect_session;
    disconnect.instance_id = "inst-refused";
    disconnect.account_id = "ident-1";
    manager.commands().push(refresh("series-browser:inst-refused:ident-1"));
    manager.commands().push(disconnect);
    manager.pump();
    REQUIRE_FALSE(
        manager.sessions().find("inst-refused", "ident-1").has_value());

    const auto settled = wait_until([&manager] {
        manager.pump();
        const auto* catalogue =
            manager.market().catalogue("inst-refused", "ident-1");
        return catalogue != nullptr
            && catalogue->state == catalogue_state::failed;
    }, std::chrono::seconds(10));
    REQUIRE(settled);

    sign_in_unreachable(manager, "inst-refused", "ident-1");

    /*
     * A catalogue the session already holds, so that a failed answer from here
     * on can only come from a read started after the second sign-in.
     */
    event held;
    held.kind = event_kind::series_catalogue;
    held.instance_id = "inst-refused";
    held.account_id = "ident-1";
    manager.events().push(held);
    manager.pump();
    REQUIRE(manager.market().catalogue("inst-refused", "ident-1")->state
        == catalogue_state::ready);

    push_command(manager, refresh("series-browser:inst-refused:ident-1"));
    const auto read_again = wait_until([&manager] {
        manager.pump();
        return manager.market().catalogue("inst-refused", "ident-1")->state
            == catalogue_state::failed;
    }, std::chrono::seconds(10));
    REQUIRE(read_again);

    manager.stop();
}

TEST_CASE("windows over one CRM matrix share its reads and its cadence",
    "[crm]") {
    const temporary_database database("crm-cadence");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    add_unreachable_instance(manager, "inst-refused");
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-1", "inst-refused");
    push_command(manager, save_account);
    sign_in_unreachable(manager, "inst-refused", "ident-1");

    const auto open_crm = [&](std::string name) {
        command open;
        open.kind = command_kind::open_window;
        open.instance_id = "inst-refused";
        open.account_id = "ident-1";
        open.new_window_kind = window_kind::crm_matrix;
        open.party_id = "p";
        open.crm_name = std::move(name);
        push_command(manager, open);
    };
    const auto batch = [&](std::string_view name) {
        return manager.crm().get("inst-refused", "ident-1", "p", name, false);
    };
    const auto settled = [&](std::string_view name) {
        return wait_until([&] {
            manager.pump();
            const auto* held = batch(name);
            return held != nullptr && !held->reading && !held->error.empty();
        }, std::chrono::seconds(10));
    };

    /* Nothing draws here: the pump alone starts the read. */
    open_crm("majors");
    REQUIRE(batch("majors") != nullptr);
    REQUIRE(batch("majors")->reading);
    REQUIRE(settled("majors"));

    const auto next = batch("majors")->next_read_ns - steady_now_ns();
    REQUIRE(next > 4'000'000'000);
    REQUIRE(next <= 5'000'000'000);

    /*
     * A second window moved onto the held matrix reads nothing until the
     * matrix's own deadline, rather than starting a read of its own.
     */
    open_crm("minors");
    REQUIRE(settled("minors"));
    command configure;
    configure.kind = command_kind::configure_crm;
    configure.window_id = "crm:inst-refused:ident-1:p:minors";
    configure.party_id = "p";
    configure.crm_name = "majors";
    push_command(manager, configure);
    for (int i = 0; i < 10; ++i)
        manager.pump();
    CHECK_FALSE(batch("majors")->reading);
    CHECK(batch("majors")->next_read_ns - steady_now_ns() > 0);

    manager.stop();
}

TEST_CASE("a restored wall is rebound by the sign-in it was saved against",
    "[windows]") {
    const temporary_database database("rebind-wall");
    save_wall_signed_in(database);

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.windows().size() == 6);

    const std::vector<std::string> restored{
        "workspace:inst-refused:ident-1",
        "series-browser:inst-refused:ident-1",
        "command-centre:inst-refused:ident-1",
        "chart:inst-refused:ident-1:s-1",
        "terms:inst-refused:ident-1:curve-1",
    };

    /*
     * Sessions are not persisted, so every window comes back open, unbound and
     * still naming the pair it was saved against.
     */
    for (const auto& id : restored) {
        const auto window = manager.windows().find(id);
        REQUIRE(window.has_value());
        REQUIRE_FALSE(manager.windows().session_of(*window).has_value());
        const auto spec = manager.windows().spec(*window, manager.sessions());
        REQUIRE(spec.session.has_value());
        REQUIRE(spec.session->instance_id == "inst-refused");
        REQUIRE(spec.session->account_id == "ident-1");
    }

    /*
     * A refresh before the sign-in moves the browser's deadline a cadence out
     * with no session to read on. The rebind must bring it back to now, or the
     * window sits on "reading" for a minute after the sign-in that made the
     * read possible.
     */
    const auto browser =
        *manager.windows().find("series-browser:inst-refused:ident-1");
    push_command(manager, refresh("series-browser:inst-refused:ident-1"));
    REQUIRE(manager.windows().next_refresh_ns(browser) > steady_now_ns());

    sign_in_unreachable(manager, "inst-refused", "ident-1");
    const auto session = manager.sessions().find("inst-refused", "ident-1");
    REQUIRE(session.has_value());

    for (const auto& id : restored) {
        const auto window = *manager.windows().find(id);
        CHECK(manager.windows().session_of(window) == session);
    }
    CHECK(manager.windows().next_refresh_ns(browser) == 0);
    CHECK(manager.windows().next_refresh_ns(
        *manager.windows().find("command-centre:inst-refused:ident-1")) == 0);
    CHECK(manager.windows().next_refresh_ns(
        *manager.windows().find("terms:inst-refused:ident-1:curve-1")) == 0);

    /*
     * The workspace draws the live tables, so its rebind has nothing to ask
     * for and leaves the deadline parked, as an open and a refresh do.
     */
    CHECK(manager.windows().next_refresh_ns(
        *manager.windows().find("workspace:inst-refused:ident-1"))
        == parked_deadline);

    /* A chart is fed by the stream, so the rebind parks its deadline. */
    CHECK(manager.windows().next_refresh_ns(
        *manager.windows().find("chart:inst-refused:ident-1:s-1"))
        == parked_deadline);

    manager.stop();
}

TEST_CASE("a restored window reopened by command is rebound and reads at once",
    "[windows]") {
    const temporary_database database("reopen-restored");
    save_wall_signed_in(database);

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /*
     * A chart opened for the pair before it signs in is unbound and names no
     * session, so the sign-in's rebind passes it over. It is the window the
     * reopen below has to bind, because nothing else will.
     */
    push_command(manager, open_window(window_kind::chart, "inst-refused",
        "ident-1", "s-2"));
    const auto orphan = *manager.windows().find("chart:inst-refused:ident-1:s-2");
    REQUIRE_FALSE(manager.windows().spec(orphan, manager.sessions())
        .session.has_value());

    sign_in_unreachable(manager, "inst-refused", "ident-1");
    const auto session = manager.sessions().find("inst-refused", "ident-1");
    REQUIRE(session.has_value());
    REQUIRE_FALSE(manager.windows().session_of(orphan).has_value());

    push_command(manager, open_window(window_kind::chart, "inst-refused",
        "ident-1", "s-2"));
    CHECK(manager.windows().session_of(orphan) == session);
    CHECK(manager.windows().next_refresh_ns(orphan) == parked_deadline);

    /*
     * The sign-in rebound the term structure and left its deadline at now. No
     * curve has been asked for yet: nothing drew the window, so nothing
     * pushed the refresh its deadline calls for.
     */
    const auto terms =
        *manager.windows().find("terms:inst-refused:ident-1:curve-1");
    REQUIRE(manager.windows().next_refresh_ns(terms) == 0);
    REQUIRE(manager.term_structures().get("inst-refused", "ident-1", "curve-1")
        == nullptr);

    push_command(manager, open_window(window_kind::term_structure,
        "inst-refused", "ident-1", "curve-1"));

    /*
     * Reopening the same curve focuses the existing window rather than making
     * a second, binds it, parks the deadline and asks for the curve itself.
     * The read goes to the refused port, so it settles as a failure, which is
     * the proof that a real read was started.
     */
    REQUIRE(manager.windows().size() == 7);
    CHECK(manager.windows().session_of(terms) == session);
    CHECK(manager.windows().next_refresh_ns(terms) == parked_deadline);
    const auto* curve =
        manager.term_structures().get("inst-refused", "ident-1", "curve-1");
    REQUIRE(curve != nullptr);
    const auto settled = wait_until([&] {
        manager.pump();
        const auto* held =
            manager.term_structures().get("inst-refused", "ident-1", "curve-1");
        return held != nullptr && !held->reading && !held->error.empty();
    }, std::chrono::seconds(10));
    REQUIRE(settled);

    /*
     * The kinds that poll come due at once when reopened, whatever their
     * deadline was before. A refresh first moves it out so the reset shows.
     */
    for (const auto& [kind, id] : {
             std::pair{window_kind::series_browser,
                 std::string{"series-browser:inst-refused:ident-1"}},
             std::pair{window_kind::command_centre,
                 std::string{"command-centre:inst-refused:ident-1"}}}) {
        const auto window = *manager.windows().find(id);
        push_command(manager, refresh(id));
        REQUIRE(manager.windows().next_refresh_ns(window) > steady_now_ns());
        push_command(manager, open_window(kind, "inst-refused", "ident-1"));
        CHECK(manager.windows().next_refresh_ns(window) == 0);
        CHECK(manager.windows().session_of(window) == session);
    }

    const auto chart = *manager.windows().find("chart:inst-refused:ident-1:s-1");
    push_command(manager, open_window(window_kind::chart, "inst-refused",
        "ident-1", "s-1"));
    CHECK(manager.windows().next_refresh_ns(chart) == parked_deadline);
    REQUIRE(manager.windows().size() == 7);

    manager.stop();
}

TEST_CASE("each window kind opens with its own cadence and deadline",
    "[windows]") {
    const temporary_database database("kind-cadence");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /*
     * What each kind starts with, pinned as the runtime does it today. A
     * parked deadline is one the frame loop never reaches, so the kind is
     * fed some other way or reads on open, and a manual refresh still reads.
     * The chart's two-second cadence is listed as it is, although nothing
     * polls on it once the deadline is parked. A kind with no read has no
     * cadence at all, and is parked at every moment.
     */
    struct expectation final {
        window_kind kind;
        std::string series_id;
        std::chrono::milliseconds cadence;
        bool parked_on_open;
        bool parked_on_refresh;
    };
    const std::vector<expectation> expected{
        {window_kind::chart, "s-1", std::chrono::seconds{2}, true, true},
        {window_kind::grid, {}, std::chrono::milliseconds{0}, true, true},
        {window_kind::notice, {}, std::chrono::milliseconds{0}, true, true},
        {window_kind::instance_manager, {}, std::chrono::milliseconds{0}, true,
            true},
        {window_kind::accounts, {}, std::chrono::milliseconds{0}, true, true},
        {window_kind::workspace, {}, std::chrono::milliseconds{0}, true, true},
        {window_kind::command_centre, {}, std::chrono::seconds{30}, false,
            false},
        {window_kind::series_browser, {}, std::chrono::seconds{60}, false,
            false},
        {window_kind::term_structure, "curve-1", std::chrono::seconds{30}, true,
            false},
        {window_kind::crm_matrix, {}, std::chrono::seconds{1}, true, true},
        {window_kind::mini_plot, "s-1", std::chrono::seconds{2}, true, true},
    };
    REQUIRE(expected.size() == hedgr::model::domain::window_kind_count);

    for (const auto& row : expected) {
        INFO("kind " << hedgr::model::domain::to_string(row.kind));
        push_command(manager, open_window(row.kind, "inst-1", "acct-1",
            row.series_id));
        const auto window = find_window_of_kind(manager, row.kind);
        REQUIRE(window.has_value());

        const auto spec = manager.windows().spec(*window, manager.sessions());
        CHECK(spec.cadence == row.cadence);
        CHECK(manager.windows().next_refresh_ns(*window)
            == (row.parked_on_open ? parked_deadline : 0));

        /*
         * A refresh rearms the poll one cadence out, except for the kinds
         * whose data arrives some other way: those stay parked.
         */
        const auto before = steady_now_ns();
        push_command(manager, refresh(std::string{manager.windows().id(*window)}));
        const auto after = steady_now_ns();
        const auto due = manager.windows().next_refresh_ns(*window);
        if (row.parked_on_refresh) {
            CHECK(due == parked_deadline);
        } else {
            const auto cadence_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    row.cadence).count();
            CHECK(due >= before + cadence_ns);
            CHECK(due <= after + cadence_ns);
        }
    }
    REQUIRE(manager.windows().size() == expected.size());

    manager.stop();
}

TEST_CASE("a refresh of a window with no read asks for nothing", "[windows]") {
    const temporary_database database("no-read-refresh");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    push_command(manager, open_window(window_kind::notice, "inst-1", "acct-1"));
    const auto window = find_window_of_kind(manager, window_kind::notice);
    REQUIRE(window.has_value());
    const std::string id{manager.windows().id(*window)};

    /*
     * The window draws the live tables, so a refresh can only be answered with
     * an event that named it and carried nothing: that event is what this
     * checks is gone, while the deadline stays where the frame loop cannot
     * reach it.
     */
    static_cast<void>(manager.events().drain());
    for (int i = 0; i < 3; ++i)
        push_command(manager, refresh(id));
    for (const auto& ev : manager.events().drain())
        CHECK(ev.payload != id);
    CHECK(manager.windows().next_refresh_ns(*window) == parked_deadline);

    manager.stop();
}

TEST_CASE("a wall the store refuses to save is a standing condition",
    "[windows]") {
    const temporary_database database("wall-refused");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.save_windows().has_value());
    REQUIRE(manager.last_error().empty());

    const auto execute = [&](std::string_view sql) {
        auto opened = hedgr::store::sqlite::database::open(database.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened->execute(std::string{sql}).has_value());
    };
    execute("CREATE TRIGGER refuse_wall_insert BEFORE INSERT ON window_spec "
            "BEGIN SELECT RAISE(ABORT, 'refused by test'); END");
    execute("CREATE TRIGGER refuse_wall_update BEFORE UPDATE ON window_spec "
            "BEGIN SELECT RAISE(ABORT, 'refused by test'); END");

    const auto refused = manager.save_windows();
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().starts_with(
        "cannot save window instance-manager: refused by test"));

    /*
     * The refusal is the one failure the status bar shows, keyed to the wall
     * and not to a window, and it stays up across commands that cannot fail:
     * the wall is still unsaved whatever the operator does next.
     */
    REQUIRE(manager.last_error() == refused.error());
    const auto rows = manager.conditions().rows();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].key.source == condition_source::window_wall);
    REQUIRE(rows[0].key.subject.empty());

    command probe_all;
    probe_all.kind = command_kind::probe_all;
    push_command(manager, probe_all);
    REQUIRE(manager.last_error() == refused.error());

    /* The save that goes through is the only thing that takes it down. */
    execute("DROP TRIGGER refuse_wall_insert");
    execute("DROP TRIGGER refuse_wall_update");
    REQUIRE(manager.save_windows().has_value());
    REQUIRE(manager.last_error().empty());
    REQUIRE(manager.conditions().empty());

    manager.stop();
}

namespace {

enum class read_scenario {
    signed_out,
    sign_in_refused,
    failed_after_sign_in,
    authenticated_without_token,
    authenticated,
};

constexpr auto read_instance = "inst-refused";
constexpr auto read_account = "ident-1";
constexpr auto read_tenant = "tenant-1";

/** @brief One store, one unreachable instance, and a session in @p scenario. */
void set_up_read_scenario(instance_manager& manager, read_scenario scenario) {
    add_unreachable_instance(manager, read_instance);
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account(read_account, read_instance);
    push_command(manager, save_account);
    switch (scenario) {
    case read_scenario::signed_out:
        return;
    case read_scenario::sign_in_refused:
        refuse_sign_in(manager, read_instance, read_account);
        return;
    case read_scenario::failed_after_sign_in: {
        sign_in_unreachable(manager, read_instance, read_account, "a-token",
            read_tenant);
        event lost;
        lost.kind = event_kind::session_failed;
        lost.instance_id = read_instance;
        lost.account_id = read_account;
        lost.message = "the connection dropped";
        manager.events().push(lost);
        manager.pump();
        const auto handle =
            manager.sessions().find(read_instance, read_account);
        REQUIRE(handle.has_value());
        REQUIRE(manager.sessions().status(*handle) == session_status::failed);
        return;
    }
    case read_scenario::authenticated_without_token:
        sign_in_unreachable(manager, read_instance, read_account, {},
            read_tenant);
        return;
    case read_scenario::authenticated:
        sign_in_unreachable(manager, read_instance, read_account, "a-token",
            read_tenant);
        return;
    }
}

/**
 * @brief One read starter, how to ask for its read and how to see it began.
 *
 * A synchronous starter marks its table before it spawns, so the mark is
 * visible at once. The others only show their read through its answer.
 */
struct read_starter final {
    const char* name;
    bool needs_token;
    bool synchronous;
    std::function<void(instance_manager&)> ask;
    std::function<bool(const instance_manager&)> began;
};

[[nodiscard]] std::vector<read_starter> read_starters() {
    const auto open = [](window_kind kind, std::string series = {},
                          std::string refresh_id = {}) {
        return [kind, series = std::move(series),
                   refresh_id = std::move(refresh_id)](
                   instance_manager& manager) {
            push_command(manager,
                open_window(kind, read_instance, read_account, series));
            if (!refresh_id.empty())
                push_command(manager, refresh(refresh_id));
        };
    };
    const auto load = [](command_kind kind) {
        return [kind](instance_manager& manager) {
            command request;
            request.kind = kind;
            request.instance_id = read_instance;
            request.account_id = read_account;
            request.party_id = "p";
            request.image_id = "img-1";
            request.curve_key = "curve-1";
            request.curve_label = "Curve";
            request.member_ids = {"m-1y", "m-2y"};
            request.member_tenors = {"1Y", "2Y"};
            push_command(manager, request);
        };
    };
    return {
        {"series browser catalogue", true, true,
            open(window_kind::series_browser, {},
                "series-browser:inst-refused:ident-1"),
            [](const instance_manager& m) {
                return m.market().catalogue(read_instance, read_account)
                    != nullptr;
            }},
        {"command centre deployment", true, false,
            open(window_kind::command_centre, {},
                "command-centre:inst-refused:ident-1"),
            [](const instance_manager& m) {
                const auto handle =
                    m.sessions().find(read_instance, read_account);
                return handle && !m.sessions().deployment(*handle).failure.empty();
            }},
        {"term structure window", false, true,
            open(window_kind::term_structure, "curve-1"),
            [](const instance_manager& m) {
                return m.term_structures().get(read_instance, read_account,
                    "curve-1") != nullptr;
            }},
        {"crm matrix window", false, true, open(window_kind::crm_matrix),
            [](const instance_manager& m) {
                return m.crm().get(read_instance, read_account, "p", "majors",
                    false) != nullptr;
            }},
        {"load_term_structure command", false, true,
            load(command_kind::load_term_structure),
            [](const instance_manager& m) {
                return m.term_structures().get(read_instance, read_account,
                    "curve-1") != nullptr;
            }},
        {"load_crm_names command", false, false,
            load(command_kind::load_crm_names),
            [](const instance_manager& m) {
                return m.crm().names(read_instance, read_account, "p")
                    != nullptr;
            }},
        {"load_image command", true, true, load(command_kind::load_image),
            [](const instance_manager& m) {
                return !m.pending_images().empty()
                    || !m.failed_images().empty();
            }},
    };
}

}

TEST_CASE("a read starter refuses a session that cannot read and starts one that can",
    "[runtime][reads]") {
    for (const auto scenario : {read_scenario::signed_out,
             read_scenario::sign_in_refused,
             read_scenario::failed_after_sign_in,
             read_scenario::authenticated_without_token,
             read_scenario::authenticated}) {
        for (const auto& starter : read_starters()) {
            const bool expected = scenario == read_scenario::authenticated
                || (scenario == read_scenario::authenticated_without_token
                    && !starter.needs_token);
            CAPTURE(starter.name, static_cast<int>(scenario), expected);

            const temporary_database database("read-preamble");
            instance_manager manager;
            REQUIRE(manager.start(database.path(), seed_options()).has_value());
            set_up_read_scenario(manager, scenario);

            starter.ask(manager);
            manager.pump();

            bool began = starter.began(manager);
            if (!starter.synchronous) {
                began = wait_until([&] {
                    manager.pump();
                    return starter.began(manager);
                }, expected ? std::chrono::seconds(10)
                            : std::chrono::milliseconds(400));
            }
            CHECK(began == expected);
            manager.stop();
        }
    }
}

TEST_CASE("the currency image map is read for a token and refused without one",
    "[runtime][reads]") {
    for (const bool with_token : {true, false}) {
        CAPTURE(with_token);
        const temporary_database database("image-map-preamble");
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        set_up_read_scenario(manager, with_token
            ? read_scenario::authenticated
            : read_scenario::authenticated_without_token);
        CHECK((manager.currency_images().get(read_tenant) != nullptr)
            == with_token);
        manager.stop();
    }
}

namespace {

/**
 * @brief A loopback listener that counts connections and drops each at once.
 *
 * A read against it fails as fast as one against a refused port, and the count
 * says how many reads reached the wire, which is the only way to see a read
 * that was deduplicated and so never started.
 */
class connection_counter final {
public:
    connection_counter()
        : acceptor_(context_, {boost::asio::ip::address_v4::loopback(), 0}) {
        accept();
        thread_ = std::thread([this] { context_.run(); });
    }

    ~connection_counter() {
        context_.stop();
        thread_.join();
    }

    connection_counter(const connection_counter&) = delete;
    connection_counter& operator=(const connection_counter&) = delete;

    [[nodiscard]] std::string url() const {
        return std::format("nats://127.0.0.1:{}",
            acceptor_.local_endpoint().port());
    }

    [[nodiscard]] unsigned count() const { return count_.load(); }

private:
    void accept() {
        acceptor_.async_accept([this](boost::system::error_code failure,
                                   boost::asio::ip::tcp::socket socket) {
            if (failure)
                return;
            count_.fetch_add(1);
            socket.close();
            accept();
        });
    }

    boost::asio::io_context context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<unsigned> count_{0};
    std::thread thread_;
};

/**
 * @brief Pumps until no connection has arrived for a quiet period.
 *
 * Every read in these tests is refused at once, so a quiet period means every
 * answer has been applied and no further read is on its way.
 */
void settle(instance_manager& manager, const connection_counter& counter) {
    auto last = counter.count();
    auto quiet_since = std::chrono::steady_clock::now();
    const auto deadline = quiet_since + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        manager.pump();
        if (counter.count() != last) {
            last = counter.count();
            quiet_since = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - quiet_since
            > std::chrono::milliseconds(400)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    FAIL("the connections never went quiet");
}

/** @brief An instance and a signed-in account that read through @p url. */
void sign_in_at(instance_manager& manager, const std::string& url) {
    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance(read_instance, "Counted", "ores.test.counted");
    save.instance_row.nats_url = url;
    save.instance_id = read_instance;
    push_command(manager, save);
    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account(read_account, read_instance);
    push_command(manager, save_account);
    sign_in_unreachable(manager, read_instance, read_account, "a-token",
        read_tenant);
}

[[nodiscard]] command load_image(std::string image_id) {
    command request;
    request.kind = command_kind::load_image;
    request.instance_id = read_instance;
    request.account_id = read_account;
    request.image_id = std::move(image_id);
    return request;
}

}

TEST_CASE("a points answer fills the table and frees the series for the next read",
    "[runtime][reads]") {
    const temporary_database database("points-answer");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    set_up_read_scenario(manager, read_scenario::authenticated);

    push_command(manager,
        open_window(window_kind::chart, read_instance, read_account, "s-1"));
    const auto chart = find_window_of_kind(manager, window_kind::chart);
    REQUIRE(chart.has_value());
    const std::string chart_id{manager.windows().id(*chart)};
    push_command(manager, refresh(chart_id));

    const auto points_version = [&manager]() -> std::uint64_t {
        const auto held =
            manager.market().find(read_instance, read_account, "s-1");
        return held ? manager.market().version(*held) : 0;
    };

    const auto first = wait_until([&] {
        manager.pump();
        return points_version() != 0;
    }, std::chrono::seconds(10));
    REQUIRE(first);
    const auto after_first = points_version();
    CHECK_FALSE(manager.market().failure(
        *manager.market().find(read_instance, read_account, "s-1")).empty());

    /*
     * The first answer cleared the series' mark, so a second request is a read
     * and its answer replaces the first. A mark left behind would drop it.
     */
    push_command(manager, refresh(chart_id));
    const auto second = wait_until([&] {
        manager.pump();
        return points_version() != after_first;
    }, std::chrono::seconds(10));
    CHECK(second);
    const auto after_second = points_version();

    event answer;
    answer.kind = event_kind::series_points;
    answer.instance_id = read_instance;
    answer.account_id = read_account;
    answer.series_id = "s-1";
    answer.observed_at_ns = 5;
    manager.events().push(answer);
    manager.pump();
    const auto filled =
        manager.market().find(read_instance, read_account, "s-1");
    REQUIRE(filled.has_value());
    CHECK(manager.market().failure(*filled).empty());
    CHECK(manager.market().version(*filled) != after_second);
    manager.stop();
}

TEST_CASE("a points answer for a series nobody holds is dropped and counted",
    "[runtime][reads]") {
    const temporary_database database("points-unheld");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    set_up_read_scenario(manager, read_scenario::authenticated);

    const auto answer = [](std::string series_id, std::string message) {
        event fact;
        fact.kind = event_kind::series_points;
        fact.instance_id = read_instance;
        fact.account_id = read_account;
        fact.series_id = std::move(series_id);
        fact.message = std::move(message);
        fact.points = {{.at_ns = 1'000'000'000, .value = 1.5}};
        fact.observed_at_ns = 5;
        return fact;
    };

    /* Nothing holds s-9, so neither a success nor a failure makes a row. */
    manager.events().push(answer("s-9", {}));
    manager.events().push(answer("s-9", "no responder"));
    manager.pump();
    CHECK(manager.unheld_points() == 2);
    CHECK_FALSE(manager.market().find(read_instance, read_account, "s-9")
                    .has_value());
    CHECK(manager.market().size() == 0);

    /* A chart's refresh interns its series, so its answer is kept. */
    push_command(manager,
        open_window(window_kind::chart, read_instance, read_account, "s-1"));
    const auto chart = find_window_of_kind(manager, window_kind::chart);
    REQUIRE(chart.has_value());
    push_command(manager, refresh(std::string{manager.windows().id(*chart)}));
    const auto held =
        manager.market().find(read_instance, read_account, "s-1");
    REQUIRE(held.has_value());

    manager.events().push(answer("s-1", {}));
    manager.pump();
    CHECK(manager.unheld_points() == 2);
    const auto fine = manager.market().columns(*held, market_level::fine);
    REQUIRE(fine.size == 1);
    CHECK(fine.value[0] == 1.5);
    manager.stop();
}

TEST_CASE("an image is read once and not asked for again while in flight or after it failed",
    "[runtime][reads]") {
    const temporary_database database("image-dedup");
    connection_counter counter;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    sign_in_at(manager, counter.url());
    settle(manager, counter);

    /*
     * Six requests in one pump, so the five after the first find it in flight.
     * The read runs on the session's actor, so it is answered once the actor's
     * connect attempt returns, which can take the connect timeout: the wait is
     * on the answer and not on the connections going quiet. The pending mark is
     * not asserted: pump drains events after the commands, and a fast failure
     * can clear it before the first pump returns, which made that line race.
     */
    for (int request = 0; request < 6; ++request)
        manager.commands().push(load_image("img-a"));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (manager.failed_images().empty()
        && std::chrono::steady_clock::now() < deadline) {
        manager.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    REQUIRE(manager.failed_images().size() == 1);
    CHECK(manager.pending_images().empty());

    /* A key that failed is not asked for again, so no read is marked. */
    push_command(manager, load_image("img-a"));
    CHECK(manager.pending_images().empty());
    manager.stop();
}

TEST_CASE("a term structure whose member lists differ is never read",
    "[runtime][reads]") {
    const temporary_database database("term-structure-mismatch");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    set_up_read_scenario(manager, read_scenario::authenticated);

    auto window = open_window(window_kind::term_structure, read_instance,
        read_account, "curve-1");
    window.member_tenors = {"1Y"};
    push_command(manager, window);
    CHECK(manager.term_structures().get(read_instance, read_account, "curve-1")
        == nullptr);

    command load;
    load.kind = command_kind::load_term_structure;
    load.instance_id = read_instance;
    load.account_id = read_account;
    load.curve_key = "curve-2";
    load.curve_label = "Curve";
    load.member_ids = {"m-1y", "m-2y"};
    load.member_tenors = {"1Y"};
    push_command(manager, load);
    CHECK(manager.term_structures().get(read_instance, read_account, "curve-2")
        == nullptr);

    load.member_tenors = {"1Y", "2Y"};
    push_command(manager, load);
    CHECK(manager.term_structures().get(read_instance, read_account, "curve-2")
        != nullptr);
    manager.stop();
}

TEST_CASE("a failed currency image map is read again on refresh and a ready one is not",
    "[runtime][reads]") {
    const temporary_database database("image-map-retry");
    connection_counter counter;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    sign_in_at(manager, counter.url());
    settle(manager, counter);
    REQUIRE(manager.currency_images().get(read_tenant)->state
        == hedgr::model::state::image_map_state::failed);

    /* A chart with no series has no read of its own, so only the map can connect. */
    push_command(manager,
        open_window(window_kind::chart, read_instance, read_account));
    const auto chart = find_window_of_kind(manager, window_kind::chart);
    REQUIRE(chart.has_value());
    auto chart_refresh = refresh(std::string{manager.windows().id(*chart)});
    chart_refresh.instance_id = read_instance;
    chart_refresh.account_id = read_account;
    settle(manager, counter);

    /*
     * A marker in place of the real refusal, so that a read started from here
     * shows itself by replacing it.
     */
    const auto mark_failed = [&manager] {
        event marker;
        marker.kind = event_kind::currency_image_map;
        marker.instance_id = read_instance;
        marker.account_id = read_account;
        marker.tenant_id = read_tenant;
        marker.message = "marker";
        manager.events().push(marker);
        manager.pump();
        REQUIRE(manager.currency_images().get(read_tenant)->error == "marker");
    };
    mark_failed();
    push_command(manager, chart_refresh);
    settle(manager, counter);
    CHECK(manager.currency_images().get(read_tenant)->error != "marker");

    event ready;
    ready.kind = event_kind::currency_image_map;
    ready.instance_id = read_instance;
    ready.account_id = read_account;
    ready.tenant_id = read_tenant;
    manager.events().push(ready);
    manager.pump();
    REQUIRE(manager.currency_images().get(read_tenant)->state
        == hedgr::model::state::image_map_state::ready);

    /* A sign-in of the same pair again must not read a map that is ready. */
    event again;
    again.kind = event_kind::session_authenticated;
    again.instance_id = read_instance;
    again.account_id = read_account;
    again.tenant_id = read_tenant;
    again.token = secret_string{std::string{"a-new-token"}};
    manager.events().push(again);
    manager.pump();
    push_command(manager, chart_refresh);
    settle(manager, counter);
    CHECK(manager.currency_images().get(read_tenant)->state
        == hedgr::model::state::image_map_state::ready);
    manager.stop();
}

TEST_CASE("a deployment snapshot lands on the session row and frees the next read",
    "[runtime][reads]") {
    const temporary_database database("deployment-snapshot");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    set_up_read_scenario(manager, read_scenario::authenticated);

    push_command(manager, open_window(window_kind::command_centre,
        read_instance, read_account));
    const auto handle = manager.sessions().find(read_instance, read_account);
    REQUIRE(handle.has_value());
    const auto centre = find_window_of_kind(manager, window_kind::command_centre);
    REQUIRE(centre.has_value());
    const std::string centre_id{manager.windows().id(*centre)};
    push_command(manager, refresh(centre_id));
    const auto failed = [&] {
        manager.pump();
        return !manager.sessions().deployment(*handle).failure.empty();
    };
    REQUIRE(wait_until(failed, std::chrono::seconds(10)));

    event snapshot;
    snapshot.kind = event_kind::deployment_snapshot;
    snapshot.instance_id = read_instance;
    snapshot.account_id = read_account;
    snapshot.deployment.tenant_total = 7;
    manager.events().push(snapshot);
    manager.pump();
    CHECK(manager.sessions().deployment(*handle).tenant_total == 7);
    CHECK(manager.sessions().deployment(*handle).failure.empty());

    /* The first snapshot cleared the mark, so this read runs and overwrites it. */
    push_command(manager, refresh(centre_id));
    CHECK(wait_until(failed, std::chrono::seconds(10)));
    CHECK(manager.sessions().deployment(*handle).tenant_total == 0);
    manager.stop();
}

namespace {

/**
 * @brief Saves an instance and an account, the instance speaking @p format.
 *
 * The instance listens at @p url, so a connection that reaches the wire is a
 * connection the guard let through.
 */
void add_formatted_instance(instance_manager& manager, const std::string& id,
    const std::string& name, const std::string& format, const std::string& url) {
    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance(id, name, "ores.test." + id);
    save.instance_row.nats_url = url;
    save.instance_row.wire_format = format;
    save.instance_id = id;
    push_command(manager, save);

    command save_account;
    save_account.kind = command_kind::save_account;
    save_account.account_row = make_account("ident-" + id, id);
    push_command(manager, save_account);
}

void connect_formatted(instance_manager& manager, const std::string& id) {
    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = id;
    connect.account_id = "ident-" + id;
    connect.principal = "alice@example.test";
    connect.password = secret_string{std::string{"not-a-real-secret"}};
    push_command(manager, connect);
}

void disconnect_formatted(instance_manager& manager, const std::string& id) {
    command disconnect;
    disconnect.kind = command_kind::disconnect_session;
    disconnect.instance_id = id;
    disconnect.account_id = "ident-" + id;
    push_command(manager, disconnect);
}

[[nodiscard]] bool has_session(const instance_manager& manager,
    const std::string& id) {
    return manager.sessions().find(id, "ident-" + id).has_value();
}

/** @brief Pumps for @p span, long enough for a connect to reach the wire. */
void pump_for(instance_manager& manager, std::chrono::milliseconds span) {
    const auto until = std::chrono::steady_clock::now() + span;
    while (std::chrono::steady_clock::now() < until) {
        manager.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

constexpr std::chrono::milliseconds wire_quiet{600};

}

namespace {

constexpr const char* refused_url = "nats://127.0.0.1:1";

/** @brief An instance in @p format with a session that is authenticated. */
void hold_format(instance_manager& manager, const std::string& id,
    const std::string& format) {
    add_formatted_instance(manager, id, "Holder", format, refused_url);
    sign_in_unreachable(manager, id, "ident-" + id);
}

[[nodiscard]] std::string wire_format_message(const instance_manager& manager) {
    for (const auto& row : manager.conditions().rows()) {
        if (row.key.source == condition_source::session
            && row.message.contains("wire format"))
            return row.message;
    }
    return {};
}

}

TEST_CASE("a connect that would change the wire format of a live session is refused",
    "[runtime][wire]") {
    const temporary_database database("wire-refused");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    hold_format(manager, "inst-live", "json");

    const connection_counter counter;
    add_formatted_instance(manager, "inst-other", "Other", "msgpack", counter.url());
    connect_formatted(manager, "inst-other");
    pump_for(manager, wire_quiet);

    const auto message = wire_format_message(manager);
    CHECK(message.contains("msgpack"));
    CHECK(message.contains("json"));
    CHECK(message.contains("Holder"));
    CHECK_FALSE(has_session(manager, "inst-other"));
    CHECK(counter.count() == 0);
    manager.stop();
}

TEST_CASE("two live sessions may share a wire format", "[runtime][wire]") {
    const temporary_database database("wire-same");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    hold_format(manager, "inst-live", "json");

    const connection_counter counter;
    add_formatted_instance(manager, "inst-other", "Other", "json", counter.url());
    connect_formatted(manager, "inst-other");

    CHECK(wire_format_message(manager).empty());
    CHECK(has_session(manager, "inst-other"));
    CHECK(wait_until([&] {
        manager.pump();
        return counter.count() > 0;
    }, std::chrono::seconds(10)));
    manager.stop();
}

TEST_CASE("a session that failed no longer holds the wire format", "[runtime][wire]") {
    const temporary_database database("wire-failed");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    add_formatted_instance(manager, "inst-live", "Holder", "json", refused_url);
    refuse_sign_in(manager, "inst-live", "ident-inst-live");

    const connection_counter counter;
    add_formatted_instance(manager, "inst-other", "Other", "msgpack", counter.url());
    connect_formatted(manager, "inst-other");

    CHECK(wire_format_message(manager).empty());
    CHECK(has_session(manager, "inst-other"));
    CHECK(wait_until([&] {
        manager.pump();
        return counter.count() > 0;
    }, std::chrono::seconds(10)));
    manager.stop();
}

TEST_CASE("disconnecting or removing the holder releases the wire format",
    "[runtime][wire]") {
    for (const bool remove_instance : {false, true}) {
        CAPTURE(remove_instance);
        const temporary_database database("wire-released");
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        hold_format(manager, "inst-live", "json");

        const connection_counter counter;
        add_formatted_instance(manager, "inst-other", "Other", "msgpack",
            counter.url());
        connect_formatted(manager, "inst-other");
        REQUIRE_FALSE(wire_format_message(manager).empty());
        REQUIRE(counter.count() == 0);

        if (remove_instance) {
            command remove;
            remove.kind = command_kind::remove_instance;
            remove.instance_id = "inst-live";
            push_command(manager, remove);
        } else {
            disconnect_formatted(manager, "inst-live");
        }
        REQUIRE_FALSE(has_session(manager, "inst-live"));

        connect_formatted(manager, "inst-other");
        CHECK(has_session(manager, "inst-other"));
        CHECK(wait_until([&] {
            manager.pump();
            return counter.count() > 0;
        }, std::chrono::seconds(10)));
        manager.stop();
    }
}

TEST_CASE("a probe waits while a live session holds another wire format",
    "[runtime][wire]") {
    const temporary_database database("wire-probe");
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    hold_format(manager, "inst-live", "json");

    const connection_counter counter;
    add_formatted_instance(manager, "inst-other", "Other", "msgpack", counter.url());
    pump_for(manager, wire_quiet);
    CHECK(counter.count() == 0);

    disconnect_formatted(manager, "inst-live");
    CHECK(wait_until([&] {
        manager.pump();
        return counter.count() > 0;
    }, std::chrono::seconds(10)));
    manager.stop();
}

TEST_CASE("each chart keeps its own view across a restart", "[windows]") {
    using hedgr::model::domain::chart_style;
    using hedgr::model::domain::chart_view;
    constexpr std::int64_t hour = std::int64_t{3600} * 1'000'000'000;
    const temporary_database database("chart-views");
    const auto view_of = [](const instance_manager& manager,
                             std::string_view id) -> std::optional<chart_view> {
        const auto window = manager.windows().find(id);
        REQUIRE(window.has_value());
        const auto* chart =
            std::get_if<chart_state>(&manager.windows().state(*window));
        REQUIRE(chart != nullptr);
        return chart->view;
    };
    const auto configure = [](std::string id, chart_view view) {
        command request;
        request.kind = command_kind::configure_chart;
        request.window_id = std::move(id);
        request.chart_view = view;
        return request;
    };
    const chart_view daily{.span_ns = 24 * hour, .style = chart_style::candles,
        .ema = true};
    const chart_view hourly{.span_ns = hour, .style = chart_style::line};

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        add_unreachable_instance(manager, "inst-refused");
        command save_account;
        save_account.kind = command_kind::save_account;
        save_account.account_row = make_account("ident-1", "inst-refused");
        push_command(manager, save_account);
        sign_in_unreachable(manager, "inst-refused", "ident-1");
        push_command(manager, open_window(window_kind::chart, "inst-refused",
            "ident-1", "s-1"));
        push_command(manager, open_window(window_kind::chart, "inst-refused",
            "ident-1", "s-2"));
        CHECK_FALSE(view_of(manager, "chart:inst-refused:ident-1:s-1"));

        push_command(manager,
            configure("chart:inst-refused:ident-1:s-1", daily));
        push_command(manager,
            configure("chart:inst-refused:ident-1:s-2", hourly));
        CHECK(view_of(manager, "chart:inst-refused:ident-1:s-1") == daily);
        CHECK(view_of(manager, "chart:inst-refused:ident-1:s-2") == hourly);
        manager.stop();
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    CHECK(view_of(manager, "chart:inst-refused:ident-1:s-1") == daily);
    CHECK(view_of(manager, "chart:inst-refused:ident-1:s-2") == hourly);
    manager.stop();
}

TEST_CASE("a party switch holds the last request and wakes the armed wait",
    "[runtime][session][party]") {
    using hedgr::runtime::worker::party_switch;

    party_switch choice;
    CHECK_FALSE(choice.take().has_value());

    const auto first = std::make_shared<std::stop_source>();
    choice.arm(first);
    CHECK_FALSE(first->stop_requested());
    choice.request("party-one");
    CHECK(first->stop_requested());
    const auto taken = choice.take();
    REQUIRE(taken.has_value());
    CHECK(*taken == "party-one");
    CHECK_FALSE(choice.take().has_value());
    choice.disarm();

    /*
     * A request that lands before the worker arms its next wait still wakes
     * that wait, which is the race the arm call exists to close.
     */
    choice.request("party-two");
    const auto second = std::make_shared<std::stop_source>();
    choice.arm(second);
    CHECK(second->stop_requested());
    const auto next = choice.take();
    REQUIRE(next.has_value());
    CHECK(*next == "party-two");
}
