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
#include "hedgr.model/command/command.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/repository/schema.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <system_error>
#include <utility>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * The same fixture the lock tests use, for the same reason: what these cases
 * are about is what the manager reports about a file, so there has to be a file
 * to report about and it must not outlive the case.
 */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.reported.{}.{}.sqlite", unique_token(),
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
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

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

/** @brief The options the application builds from the environment. */
[[nodiscard]] session_options seed_options() {
    session_options options;
    options.nats_url = "nats://localhost:20405";
    options.subject_prefix = "ores.dev.brave_hopper";
    return options;
}

/** @brief Pushes one command and pumps the frame that applies it. */
void push_command(instance_manager& manager, const command& request) {
    manager.commands().push(command{request});
    manager.pump();
}

}

TEST_CASE("a store the build cannot read is reported, not left to a log",
    "[reported]") {
    /*
     * The defect this file exists for. A store left at an older schema makes
     * @c start fail, which is the one failure that leaves the interface with no
     * rows to draw: the window table is empty, so nothing on screen can say
     * what happened. The reason has to be readable off the manager, and it has
     * to name the file and both versions, because those are the facts an
     * operator acts on.
     *
     * The store is built by this manager and then moved back to version 6
     * directly, which is exactly what an earlier build left behind.
     */
    const temporary_database database;

    instance_manager seed_run;
    REQUIRE(seed_run.start(database.path(), seed_options()).has_value());
    REQUIRE(seed_run.startup_failure().empty());
    seed_run.stop();

    {
        auto opened = hedgr::store::sqlite::database::open(database.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened->execute("UPDATE schema_version SET version = 6")
                    .has_value());
    }

    instance_manager refused;
    const auto started = refused.start(database.path(), seed_options());

    REQUIRE_FALSE(started.has_value());

    const auto reason = refused.startup_failure();
    REQUIRE_FALSE(reason.empty());

    /* The path, so the operator knows which file to delete. */
    REQUIRE(reason.find(database.path().string()) != std::string::npos);

    /* Both versions, so they can tell a stale file from a corrupt one. */
    REQUIRE(reason.find("schema version 6") != std::string::npos);
    REQUIRE(reason.find(std::format("expects version {}",
                         hedgr::store::repository::schema_version()))
        != std::string::npos);

    /* And what to do about it. */
    REQUIRE(reason.find("delete the file and start again") != std::string::npos);

    /* The same sentence is the one the runtime's running note carries. */
    REQUIRE(refused.last_error() == reason);

    /*
     * And it stays. A startup failure is a standing condition, not a verdict
     * on a command: nothing this process does can open the store it refused,
     * so the next thing the operator tries must not take the sentence down.
     * The one-line note moves on to that attempt's own refusal, which is the
     * newer of the two, and the startup row is still in the table beside it.
     */
    command attempted;
    attempted.kind = command_kind::open_window;
    attempted.new_window_kind = hedgr::model::domain::window_kind::chart;
    push_command(refused, attempted);

    REQUIRE(refused.last_error() == "cannot open a window: the store is not open");
    const auto rows = refused.conditions().rows();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].key.source == hedgr::model::state::condition_source::startup);
    REQUIRE(rows[0].message == reason);
}

TEST_CASE("a seed write the store refuses is a start failure", "[reported]") {
    /*
     * A store that refuses the seeded instance must not come up showing a row
     * no store holds: the next run would not find it and would seed it again.
     * The refusal is a trigger, so the store itself says no and the error is
     * the repository's own.
     */
    setenv("HEDGR_ORESTUDIO_ROOT", "", 1);
    const temporary_database database;
    {
        auto opened = hedgr::store::sqlite::database::open(database.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened->apply_schema().has_value());
        REQUIRE(opened->execute(
                    "CREATE TRIGGER refuse_instance BEFORE INSERT ON instance "
                    "BEGIN SELECT RAISE(ABORT, 'refused by test'); END")
                    .has_value());
    }

    instance_manager manager;
    const auto started = manager.start(database.path(), seed_options());

    REQUIRE_FALSE(started.has_value());
    REQUIRE_FALSE(manager.is_started());

    REQUIRE(manager.instances().size() == 1);
    const auto seeded_id = manager.instances().rows().front().id;
    using hedgr::model::state::condition_source;

    /*
     * The instance refusal is not the only one: the seeded logins reference
     * the instance the store never took, so both are refused after it, and
     * every refusal is kept in the order the seeder met it.
     */
    const auto rows = manager.conditions().rows();
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].key.source == condition_source::instance);
    REQUIRE(rows[0].key.subject == seeded_id);
    REQUIRE(rows[0].message == std::format(
        "cannot save the seeded instance {}: refused by test (step_failed)",
        seeded_id));
    REQUIRE(rows[1].key.source == condition_source::account);
    REQUIRE(rows[1].key.subject == std::format("acct:{}:super admin", seeded_id));
    REQUIRE(rows[1].message.starts_with(std::format(
        "cannot save the seeded account acct:{}:super admin: ", seeded_id)));
    REQUIRE(rows[2].key.source == condition_source::account);
    REQUIRE(rows[2].key.subject == std::format("acct:{}:tenant admin", seeded_id));

    /* The start failure is the newest refusal, and the bar reads the same. */
    REQUIRE(started.error() == rows[2].message);
    REQUIRE(manager.startup_failure() == rows[2].message);
    REQUIRE(manager.last_error() == rows[2].message);
}

TEST_CASE("the reported failure is the last attempt's, not an old one",
    "[reported]") {
    /*
     * The reason the runtime clears its note as each command is applied. The
     * interface shows this string, so a note that was only ever written would
     * leave the bar reporting the refusal of something the operator has since
     * done successfully -- a failure the store does not have.
     */
    const temporary_database database;

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    command refused;
    refused.kind = command_kind::remove_folder;
    refused.folder_row.id = "instances-root";
    push_command(manager, refused);

    REQUIRE(manager.last_error() ==
        "cannot remove folder instances-root: it is the root of the instances "
        "tree");

    /* A command the store accepts, which is the operator's next action. */
    command accepted;
    accepted.kind = command_kind::open_window;
    accepted.new_window_kind = hedgr::model::domain::window_kind::chart;
    push_command(manager, accepted);

    REQUIRE(manager.last_error().empty());

    manager.stop();
}
