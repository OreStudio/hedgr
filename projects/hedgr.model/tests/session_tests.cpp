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
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/domain/window_json_io.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

namespace domain = hedgr::model::domain;

TEST_CASE("a session's mode follows its tenant", "[session]") {
    /*
     * The deployment's own rule, stated once here so the interface never has to
     * hold it: the system tenant's session acts on the deployment, and every
     * other tenant's acts inside itself.
     */
    REQUIRE(domain::session_mode_for(domain::system_tenant_id)
        == domain::session_mode::system_administration);
    REQUIRE(domain::session_mode_for("590a42e4-c418-4cf2-8fa2-22ee632ada3d")
        == domain::session_mode::application);
    REQUIRE(domain::session_mode_for("") == domain::session_mode::application);

    REQUIRE(domain::to_string(domain::session_mode::system_administration)
        == "system_administration");
    REQUIRE(domain::to_string(domain::session_mode::application)
        == "application");
}

TEST_CASE("a command centre state survives the stored form", "[window]") {
    /*
     * The window wall is restored from the store, so the state has to make the
     * round trip. The instance is what a restored window names when there is no
     * live session to ask, which is why it is persisted at all.
     */
    const domain::window_state state =
        domain::command_centre_state{"inst-clever_dijkstra"};

    const auto encoded =
        domain::window_state_to_json(domain::window_kind::command_centre, state);
    REQUIRE(encoded.has_value());

    const auto decoded = domain::window_state_from_json(
        domain::window_kind::command_centre, *encoded);
    REQUIRE(decoded.has_value());

    const auto* centre = std::get_if<domain::command_centre_state>(&*decoded);
    REQUIRE(centre != nullptr);
    REQUIRE(centre->instance_id == "inst-clever_dijkstra");
}
