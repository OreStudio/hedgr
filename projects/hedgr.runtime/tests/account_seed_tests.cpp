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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <string_view>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::folder;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief A token no other process or run is likely to pick.
 *
 * The suite opens real files, so a path derived only from a counter is unique
 * within one process and repeats on the next run. A value drawn from the
 * machine's entropy makes a collision impossible rather than unlikely.
 */
[[nodiscard]] std::string unique_token() {
    std::random_device source;
    return std::format("{:08x}{:08x}", source(), source());
}

/** @brief A database file that deletes itself, sidecars included. */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.account_seed.{}.{}.sqlite", unique_token(),
                  next_serial())) {
        erase();
    }

    ~temporary_database() { erase(); }

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

[[nodiscard]] session_options seed_options() {
    return session_options{
        .nats_url = "nats://localhost:20405",
        .subject_prefix = "ores.dev.brave_hopper",
    };
}

/**
 * @brief Pins the environment scan at no checkouts.
 *
 * The suite already does this through the test properties, and this repeats it
 * for a binary run by hand: the seeding case has to see the deployed-package
 * shape, one instance from the process's own options, not whatever checkouts
 * happen to sit beside the repository on this machine.
 */
void pin_no_checkouts() {
    setenv("HEDGR_ORESTUDIO_ROOT", "", 1);
}

/** @brief The account with @p id, or nullptr. */
[[nodiscard]] const account* find_account(const instance_manager& manager,
    std::string_view id) {
    const auto handle = manager.accounts().find(id);
    return handle ? manager.accounts().get(*handle) : nullptr;
}

/** @brief The account key the seeder derives for one instance and name. */
[[nodiscard]] std::string seeded_account_id(std::string_view instance_id,
    std::string_view name) {
    return std::format("acct:{}:{}", instance_id, name);
}

/**
 * @brief The one folder named @p name in @p instance_id's accounts tree.
 *
 * Found by walking the table rather than by rebuilding the seeder's derived
 * key, so the case reads what the manager wrote rather than repeating the
 * formula the manager used. A folder found through a predicted id would not
 * prove the seeder wrote it.
 */
[[nodiscard]] const folder* find_accounts_folder(const instance_manager& manager,
    std::string_view instance_id, std::string_view name) {
    for (const auto& row : manager.folders().rows()) {
        if (row.tree == folder_tree::accounts && row.owner_id == instance_id &&
            row.name == name)
            return &row;
    }
    return nullptr;
}

/**
 * @brief How many folders named @p name sit in @p instance_id's accounts tree.
 *
 * A count rather than a lookup, because the point of a second seed is that it
 * converges on one folder and a lookup could not tell one from two.
 */
[[nodiscard]] std::size_t count_accounts_folders(
    const instance_manager& manager, std::string_view instance_id,
    std::string_view name) {
    std::size_t result = 0;
    for (const auto& row : manager.folders().rows()) {
        if (row.tree == folder_tree::accounts && row.owner_id == instance_id &&
            row.name == name)
            ++result;
    }
    return result;
}

/** @brief The id of the one instance named @p name, or empty. */
[[nodiscard]] std::string instance_id_named(const instance_manager& manager,
    std::string_view name) {
    for (const auto& row : manager.instances().rows()) {
        if (row.name == name)
            return row.id;
    }
    return {};
}

/** @brief Applies @p request and pumps once, as the frame loop would. */
void push_command(instance_manager& manager, command request) {
    manager.commands().push(std::move(request));
    manager.pump();
}

}

/*
 * The operator's report is empty Accounts windows, and the fix is that every
 * instance comes up with two logins rather than none. These cases hold the
 * runtime to that: the rows are asserted where the Accounts window reads them,
 * in the manager's table, rather than by trusting the seeder's own accounting.
 */
TEST_CASE("a started store holds the two seeded accounts of its instance",
    "[seed]") {
    pin_no_checkouts();
    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());

    /*
     * One instance comes from this process's options, and it is the only one
     * the walk below can have seeded, so the table holds exactly its two rows.
     */
    REQUIRE(manager.instances().size() == 1);
    const std::string instance_id = manager.instances().rows()[0].id;
    REQUIRE(manager.accounts().size() == 2);

    const auto* super = find_account(manager,
        seeded_account_id(instance_id, "super admin"));
    REQUIRE(super != nullptr);
    REQUIRE(super->name == "super admin");
    REQUIRE(super->instance_id == instance_id);
    /*
     * The principal is the instance's own subject prefix with the role in
     * front, which is the name IAM is asked to recognise.
     */
    REQUIRE(super->principal == "super-admin@ores.dev.brave_hopper");

    const auto* tenant = find_account(manager,
        seeded_account_id(instance_id, "tenant admin"));
    REQUIRE(tenant != nullptr);
    REQUIRE(tenant->name == "tenant admin");
    REQUIRE(tenant->instance_id == instance_id);
    REQUIRE(tenant->principal == "tenant-admin@ores.dev.brave_hopper");

    /*
     * Both sit in a Privileged folder of the instance's accounts tree, and
     * that folder hangs off the accounts root. A row parented anywhere else
     * would be a row the Accounts window could not show under the tree it
     * draws, and a row written silently into the root would be one the
     * operator never asked for.
     */
    const auto* privileged =
        find_accounts_folder(manager, instance_id, "Privileged");
    REQUIRE(privileged != nullptr);
    REQUIRE(privileged->tree == folder_tree::accounts);
    REQUIRE(privileged->owner_id == instance_id);
    REQUIRE(privileged->parent_id ==
        std::format("accounts-root:{}", instance_id));
    REQUIRE(super->folder_id == privileged->id);
    REQUIRE(tenant->folder_id == privileged->id);

    const auto accounts_root =
        manager.folders().root(folder_tree::accounts, instance_id);
    REQUIRE(accounts_root.has_value());
    REQUIRE(privileged->parent_id ==
        manager.folders().get(*accounts_root)->id);

    manager.stop();
}

TEST_CASE("an instance already in the store is seeded on the next start",
    "[seed]") {
    pin_no_checkouts();
    const temporary_database database;

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());

        /*
         * A second instance is written the way the operator writes one, through
         * the command channel rather than through the seed. Seeding runs at
         * start, so this one has no accounts yet and the table still holds only
         * the rows of the instance the start seeded.
         */
        command save;
        save.kind = command_kind::save_instance;
        instance row;
        row.id = "inst-late";
        row.name = "late arrival";
        row.nats_url = "nats://localhost:20405";
        row.subject_prefix = "ores.staging.late";
        save.instance_row = std::move(row);
        save.instance_id = "inst-late";
        push_command(manager, save);

        REQUIRE(manager.instances().size() == 2);
        REQUIRE(manager.accounts().size() == 2);
        manager.stop();
    }

    /*
     * A restart is what reaches the instance the first start never saw. Two
     * instances now hold two accounts each, which is the whole promise: an
     * instance in the store comes up with its logins whether this build seeded
     * it or found it.
     */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.last_error().empty());
    REQUIRE(reopened.accounts().size() == 4);

    const std::string late = instance_id_named(reopened, "late arrival");
    REQUIRE(late == "inst-late");
    const auto* super = find_account(reopened,
        seeded_account_id(late, "super admin"));
    REQUIRE(super != nullptr);
    REQUIRE(super->principal == "super-admin@ores.staging.late");
    const auto* tenant = find_account(reopened,
        seeded_account_id(late, "tenant admin"));
    REQUIRE(tenant != nullptr);

    /*
     * The instance the restart found gets the same one Privileged folder the
     * first start gave the instance it seeded, and both of its logins name
     * it. The folder is the thing the two starts have to agree on for the
     * tree to hold one folder rather than a mixture.
     */
    const std::string local = instance_id_named(reopened, "local development");
    REQUIRE(!local.empty());
    REQUIRE(count_accounts_folders(reopened, local, "Privileged") == 1);
    REQUIRE(count_accounts_folders(reopened, late, "Privileged") == 1);
    const auto* late_privileged =
        find_accounts_folder(reopened, late, "Privileged");
    REQUIRE(late_privileged != nullptr);
    REQUIRE(late_privileged->parent_id ==
        std::format("accounts-root:{}", late));
    REQUIRE(super->folder_id == late_privileged->id);
    REQUIRE(tenant->folder_id == late_privileged->id);

    /* A third start converges on the same four rows rather than adding two. */
    instance_manager again;
    REQUIRE(again.start(database.path(), seed_options()).has_value());
    REQUIRE(again.accounts().size() == 4);
    /*
     * One Privileged folder per instance on the third start as on the second.
     * The derived id is what stops the folder being added again beside the
     * one an earlier start already wrote.
     */
    REQUIRE(count_accounts_folders(again, local, "Privileged") == 1);
    REQUIRE(count_accounts_folders(again, late, "Privileged") == 1);
    again.stop();

    reopened.stop();
}
