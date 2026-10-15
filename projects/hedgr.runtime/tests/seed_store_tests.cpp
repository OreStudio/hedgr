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
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <thread>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::folder;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::label;
using hedgr::model::domain::label_colour;
using hedgr::model::domain::label_severity;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::store_lock;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief The password the seeded store is sealed with.
 *
 * Read from the variable the capture script uses, so a seeder and the client
 * that later opens the store agree on the secret without an override, and so
 * the password never appears on a command line where the process table would
 * show it. The fallback matches capture.sh's, because a capture that set
 * nothing must still produce a store the client can open.
 *
 * It is a fixture and not a secret: it exists only in the store this test has
 * just written under the directory the capture gave it. The client that opens
 * that store is told, on screen, that the password came from the environment
 * rather than from the person at the keyboard.
 */
[[nodiscard]] std::string seed_password() {
    if (const char* from_env = std::getenv("HEDGR_MASTER_PASSWORD");
        from_env != nullptr && *from_env != '\0')
        return from_env;
    return "Secure-Password-123";
}

/**
 * @brief Pumps the manager until @p ready, or the limit passes.
 *
 * A bounded wait rather than a sleep, so a failure fails the test instead of
 * hanging the suite on a worker that never ran. The unlock is a coroutine on
 * the runtime's pool by design, so its outcome does not arrive on the frame
 * that pushed the command.
 */
[[nodiscard]] bool pump_until(instance_manager& manager,
    const std::function<bool()>& ready, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        manager.pump();
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    manager.pump();
    return ready();
}

/** @brief Pushes @p request and pumps until @p ready, or the limit passes. */
[[nodiscard]] bool run_until(instance_manager& manager, command request,
    const std::function<bool()>& ready) {
    manager.commands().push(std::move(request));
    return pump_until(manager, ready, std::chrono::seconds(20));
}

}

/*
 * This file exists for one reason: a screenshot of a browser needs rows in it,
 * and the only way to get rows into a store is to write them through the
 * runtime that owns the schema and the sealing.
 *
 * It is off unless HEDGR_SEED_DB names a file, which is how the capture
 * procedure asks for a populated store. With the variable unset the case passes
 * immediately and does nothing, so a normal test run is unaffected -- the test
 * is the seeder, because a second executable whose only job was to write rows
 * would be a second place the schema is spoken.
 */
TEST_CASE("a store can be seeded for a capture", "[seed]") {
    const char* requested = std::getenv("HEDGR_SEED_DB");
    if (requested == nullptr || *requested == '\0') {
        SUCCEED("no seed was requested");
        return;
    }

    const std::filesystem::path path{requested};
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
    std::filesystem::create_directories(path.parent_path(), ignored);

    /*
     * A capture shows what this case writes and nothing else, so the seeder's
     * scan of the OreStudio checkouts is turned off here. Left on, every
     * environment beside the repository would be seeded as well and the counts
     * asserted below would be about the operator's machine rather than about
     * what the capture asked for.
     */
    setenv("HEDGR_ORESTUDIO_ROOT", "", 1);

    instance_manager manager;
    REQUIRE(manager.start(path, session_options{
        .nats_url = "nats://localhost:20405",
        .subject_prefix = "ores.dev.brave_hopper",
    }).has_value());

    command create;
    create.kind = command_kind::set_master_password;
    create.password = std::string{seed_password()};
    REQUIRE(run_until(manager, std::move(create),
        [&manager] { return manager.store_lock() == store_lock::unlocked; }));

    const auto save_folder = [&](std::string id, std::string parent,
                                 std::string name) {
        command request;
        request.kind = command_kind::save_folder;
        request.folder_row.id = std::move(id);
        request.folder_row.parent_id = std::move(parent);
        request.folder_row.name = std::move(name);
        manager.commands().push(std::move(request));
    };

    save_folder("f-prod", std::string{hedgr::model::domain::instances_root_id},
        "Production");
    save_folder("f-dev", std::string{hedgr::model::domain::instances_root_id},
        "Development");
    save_folder("f-dev-eu", "f-dev", "Europe");
    manager.pump();

    /*
     * The runtime seeds one instance from the options this process started
     * with, and an instance's name is unique in the store. The capture should
     * show that instance rather than a twin of it, so its id is read back and
     * its name is left alone: a seeded store that held two rows called "local
     * development" would be a store whose second row was refused, which is a
     * more confusing thing to look at than either.
     */
    const auto find_seeded = [&](std::string_view name) {
        for (const auto& row : manager.instances().rows()) {
            if (row.name == name)
                return row.id;
        }
        return std::string{};
    };
    const std::string seeded_id = find_seeded("local development");
    REQUIRE_FALSE(seeded_id.empty());

    const auto save_instance = [&](std::string id, std::string name,
                                   std::string url, std::string prefix,
                                   std::string folder_id) {
        command request;
        request.kind = command_kind::save_instance;
        /*
         * The id is copied into both fields rather than moved into the first:
         * the two assignments are sequenced, and a move would leave the second
         * one naming nothing.
         */
        request.instance_row.id = id;
        request.instance_id = id;
        request.instance_row.name = std::move(name);
        request.instance_row.nats_url = std::move(url);
        request.instance_row.subject_prefix = std::move(prefix);
        request.instance_row.folder_id = std::move(folder_id);
        manager.commands().push(std::move(request));
        manager.pump();
    };

    /*
     * The seeded instance is moved into a folder of its own rather than
     * replaced, so the capture shows a tree with a leaf that came from the
     * running process as well as ones the seed added.
     */
    {
        command move;
        move.kind = command_kind::save_instance;
        move.instance_row = *manager.instances().get(*manager.instances().find(seeded_id));
        move.instance_row.folder_id = "f-dev";
        move.instance_id = seeded_id;
        manager.commands().push(std::move(move));
        manager.pump();
    }
    save_instance("inst-prod", "production cluster", "nats://manta:20405",
        "ores.prod.manta", "f-prod");
    save_instance("inst-lab", "laboratory", "nats://lab-01:4222",
        "ores.lab.one", "f-dev-eu");

    const auto save_label = [&](std::string name, label_colour colour,
                                label_severity severity) {
        command request;
        request.kind = command_kind::save_label;
        request.label_row.id = std::format("label:{}", name);
        request.label_row.name = name;
        request.label_row.colour = colour;
        request.label_row.severity = severity;
        manager.commands().push(std::move(request));
    };

    const auto attach = [&](labelling_owner owner, std::string owner_id,
                            std::string label_name) {
        command request;
        request.kind = command_kind::attach_label;
        request.labelling_row.owner = owner;
        request.labelling_row.owner_id = std::move(owner_id);
        request.labelling_row.label_id = std::format("label:{}", label_name);
        manager.commands().push(std::move(request));
    };

    save_label("production", label_colour::red, label_severity::danger);
    save_label("critical", label_colour::amber, label_severity::caution);
    save_label("internal", label_colour::blue, label_severity::none);
    attach(labelling_owner::instance, "inst-prod", "production");
    attach(labelling_owner::instance, "inst-prod", "critical");
    attach(labelling_owner::instance, seeded_id, "internal");
    manager.pump();

    const auto save_account = [&](std::string id, std::string instance_id,
                                  std::string name, std::string principal,
                                  std::string folder_id) {
        command request;
        request.kind = command_kind::save_account;
        request.account_row.id = id;
        request.account_id = id;
        request.account_row.instance_id = std::move(instance_id);
        request.account_row.name = std::move(name);
        request.account_row.principal = std::move(principal);
        request.account_row.folder_id = std::move(folder_id);
        manager.commands().push(std::move(request));
        manager.pump();
    };

    const auto accounts_root =
        hedgr::model::domain::accounts_root_id_for(seeded_id);
    save_account("acct-ops", seeded_id, "ops read-only", "ops@brave-hopper",
        accounts_root);
    save_account("acct-admin", seeded_id, "administrator", "admin@brave-hopper",
        accounts_root);
    attach(labelling_owner::account, "acct-admin", "production");
    manager.pump();

    save_folder("f-ops", accounts_root, "Operations");
    manager.pump();

    /*
     * One account is moved into a subfolder of its instance's accounts tree, so
     * that a capture shows a folder with something under it rather than the
     * root holding everything. The row is read from the table rather than
     * rebuilt here: the manager is the only writer, and a row assembled by
     * hand would be a second description of the same thing.
     */
    /*
     * The root is named rather than assumed: it is what a capture's accounts
     * tree hangs from, and a root that is missing would produce a capture of an
     * empty tree with nothing saying why.
     */
    REQUIRE(manager.folders().root(folder_tree::accounts, seeded_id).has_value());

    const auto ops = manager.accounts().find("acct-ops");
    REQUIRE(ops.has_value());
    command move;
    move.kind = command_kind::save_account;
    move.account_row = *manager.accounts().get(*ops);
    move.account_row.folder_id = "f-ops";
    manager.commands().push(std::move(move));
    manager.pump();

    REQUIRE(manager.last_error().empty());

    /*
     * The two surfaces the capture has to show are opened here rather than
     * clicked, because a capture is not a person. They are opened through the
     * runtime's own calls, so what the capture restores is a window wall the
     * application itself wrote.
     */
    const std::string accounts_window = manager.open_accounts_window(seeded_id);
    REQUIRE(accounts_window == manager.windows().id(*manager.windows().find(
                accounts_window)));


    command open_workspace;
    open_workspace.kind = command_kind::open_window;
    open_workspace.instance_id = seeded_id;
    open_workspace.account_id = "acct-ops";
    open_workspace.new_window_kind = hedgr::model::domain::window_kind::workspace;
    manager.commands().push(std::move(open_workspace));
    manager.pump();

    /* The store is written before the process leaves, so the capture finds it. */
    REQUIRE(manager.save_windows().has_value());

    /*
     * The seeder asserts on what it wrote rather than trusting the queue: a
     * store that is empty because a command was refused would produce a capture
     * of an empty browser and no explanation.
     */
    REQUIRE(manager.instances().size() == 3);
    REQUIRE(manager.accounts().size() == 2);
    REQUIRE(manager.folders().size() == 8);
    REQUIRE(manager.labels().size() == 3);
    for (const auto& id : std::array<std::string, 3>{ seeded_id, "inst-prod",
             "inst-lab" }) {
        INFO("instance " << id);
        REQUIRE(manager.instances().find(id).has_value());
    }
}
