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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/tables.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <string>
#include <utility>

using hedgr::model::domain::account;
using hedgr::model::domain::instance;
using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::model::domain::window_spec;
using hedgr::model::state::account_table;
using hedgr::model::state::instance_table;
using hedgr::model::state::session_table;
using hedgr::model::state::window_table;

/** @brief An instance row carrying only what a table test reads back. */
[[nodiscard]] instance make_instance(std::string id, std::string name) {
    instance row;
    row.id = std::move(id);
    row.name = std::move(name);
    return row;
}

/** @brief An account row belonging to @p instance_id. */
[[nodiscard]] account make_account(std::string id, std::string instance_id) {
    account row;
    row.id = std::move(id);
    row.instance_id = std::move(instance_id);
    return row;
}

TEST_CASE("an instance is found by id and read back by handle", "[instance]") {
    instance_table instances;

    const auto handle = instances.upsert(make_instance("inst-1", "Local"));

    REQUIRE(instances.size() == 1);
    REQUIRE(instances.contains(handle));
    REQUIRE(instances.find("inst-1") == handle);
    REQUIRE(instances.get(handle) != nullptr);
    REQUIRE(instances.get(handle)->name == "Local");
}

TEST_CASE("a replaced instance keeps the handle it held", "[instance]") {
    instance_table instances;
    const auto first = instances.upsert(make_instance("inst-1", "Local"));

    const auto second = instances.upsert(make_instance("inst-1", "Renamed"));

    REQUIRE(second == first);
    REQUIRE(instances.size() == 1);
    REQUIRE(instances.get(first)->name == "Renamed");
}

TEST_CASE("a removed instance leaves a hole no handle reaches", "[instance]") {
    instance_table instances;
    const auto first = instances.upsert(make_instance("inst-1", "Local"));

    instances.remove(first);

    REQUIRE(instances.size() == 0);
    REQUIRE_FALSE(instances.contains(first));
    REQUIRE(instances.get(first) == nullptr);
    REQUIRE_FALSE(instances.find("inst-1").has_value());

    /* The slot stays where it was, which keeps every other handle valid. */
    REQUIRE(instances.rows().size() == 1);
    REQUIRE_FALSE(instances.layout().is_alive(0));

    const auto second = instances.upsert(make_instance("inst-2", "Next"));
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);
    REQUIRE_FALSE(second == first);
}

TEST_CASE("an account table reports the children of an instance", "[account]") {
    account_table accounts;
    const auto first = accounts.upsert(make_account("acc-1", "inst-1"));
    static_cast<void>(accounts.upsert(make_account("acc-2", "inst-2")));
    static_cast<void>(accounts.upsert(make_account("acc-3", "inst-1")));

    REQUIRE(accounts.children_of("inst-1").size() == 2);
    REQUIRE(accounts.children_of("inst-2").size() == 1);

    accounts.remove(first);

    const auto children = accounts.children_of("inst-1");
    REQUIRE(children.size() == 1);
    REQUIRE(children.front()->id == "acc-3");
}

TEST_CASE("a session is found by the instance and account pair", "[session]") {
    session_table sessions;
    const auto handle = sessions.insert("inst-1", "id-1");

    REQUIRE(sessions.size() == 1);
    REQUIRE(sessions.status(handle) == session_status::disconnected);
    REQUIRE(sessions.find("inst-1", "id-1") == handle);
    REQUIRE_FALSE(sessions.find("inst-1", "id-2").has_value());
}

TEST_CASE("a released handle does not address the next occupant", "[session]") {
    session_table sessions;
    const auto first = sessions.insert("inst-1", "id-1");
    sessions.remove(first);

    REQUIRE(sessions.size() == 0);
    REQUIRE_FALSE(sessions.contains(first));

    const auto second = sessions.insert("inst-1", "id-1");
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);
    REQUIRE_FALSE(second == first);
    REQUIRE(sessions.contains(second));
}

TEST_CASE("removing a session drops the token it held", "[session]") {
    session_table sessions;
    const auto handle = sessions.insert("inst-1", "id-1");
    sessions.set_account_result(handle, "acme", "books", "secret-token");

    REQUIRE(sessions.tenant_name(handle) == "acme");
    sessions.remove(handle);

    REQUIRE_FALSE(sessions.contains(handle));
    REQUIRE(sessions.size() == 0);
}

TEST_CASE("the status column stays stride-1 over holes", "[session]") {
    session_table sessions;
    const auto first = sessions.insert("inst-1", "id-1");
    const auto second = sessions.insert("inst-1", "id-2");
    sessions.set_status(second, session_status::authenticated);
    sessions.remove(first);

    const auto column = sessions.status_column();
    REQUIRE(column.size() == 2);
    REQUIRE(column[second.index] ==
            static_cast<std::uint8_t>(session_status::authenticated));
}

TEST_CASE("a window keeps its stable id across a reopen", "[window]") {
    window_table windows;
    const auto spec = window_spec{
        .id = "w-trades",
        .kind = window_kind::grid,
    };
    const auto handle = windows.insert(spec);

    REQUIRE(windows.size() == 1);
    REQUIRE(windows.is_open(handle));
    REQUIRE(windows.find("w-trades") == handle);
    REQUIRE(windows.kind(handle) == window_kind::grid);

    windows.set_open(handle, false);
    REQUIRE_FALSE(windows.is_open(handle));

    windows.set_open(handle, true);
    REQUIRE(windows.find("w-trades") == handle);
}

TEST_CASE("a window description round-trips through the table", "[window]") {
    session_table sessions;
    window_table windows;
    const auto session = sessions.insert("inst-1", "id-1");

    window_spec spec;
    spec.id = "w-chart";
    spec.kind = window_kind::chart;
    spec.session = hedgr::model::domain::session_key{"inst-1", "id-1"};
    const auto handle = windows.insert(spec);
    windows.bind_session(handle, session);

    REQUIRE(windows.session_of(handle) == session);

    const auto recovered = windows.spec(handle, sessions);
    REQUIRE(recovered.id == "w-chart");
    REQUIRE(recovered.kind == window_kind::chart);
    REQUIRE(recovered.session.has_value());
    REQUIRE(recovered.session->instance_id == "inst-1");
    REQUIRE(recovered.session->account_id == "id-1");

    REQUIRE(windows.specs(sessions).size() == 1);
}
