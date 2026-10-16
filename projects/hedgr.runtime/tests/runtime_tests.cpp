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
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.runtime/worker/probe_worker.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <random>
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
using hedgr::model::domain::account;
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
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
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
void add_unreachable_instance(instance_manager& manager, std::string id) {
    command save;
    save.kind = command_kind::save_instance;
    save.instance_row = make_instance(std::move(id), "Refused", "ores.test.refused");
    save.instance_row.nats_url = "nats://127.0.0.1:1";
    save.instance_id = save.instance_row.id;
    push_command(manager, save);
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
    connect.password = "not-a-real-secret";
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
    connect.password = "not-a-real-secret";
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
