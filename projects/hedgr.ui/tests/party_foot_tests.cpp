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
#include "headless_desk.hpp"
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/windows.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

using namespace hedgr::testing;

TEST_CASE("a foot selection asks for a different party only", "[party][foot]") {
    render::party_list parties;
    REQUIRE(render::update_party_list(parties,
        "p1\x1f" "First party\x1ep2\x1f" "Second party", "p1",
        "First party"));
    REQUIRE(parties.size() == 2);

    CHECK_FALSE(render::acted_party(parties, 0, "p1").has_value());
    CHECK(render::acted_party(parties, 1, "p1") == "p2");
    CHECK_FALSE(render::acted_party(parties, 2, "p1").has_value());
    CHECK_FALSE(render::acted_party(parties, -1, "p1").has_value());
}

TEST_CASE("a window's foot learns the session's parties", "[party][foot]") {
    headless_imgui imgui;
    desk desk;
    desk.sessions.set_parties(desk.session,
        "p1\x1f" "First party\x1ep2\x1f" "Second party");
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "b");

    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    frame([&] {
        render::render_series_browser(handle, input, state, status, lock);
    });

    /* The control's list is the session's, and it starts on the held party. */
    CHECK(state.parties.size() == 2);
    CHECK(state.parties.names.size() == 2);
    CHECK(state.acting_party == 0);
}
