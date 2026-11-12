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
#include "hedgr.ui/render/panels.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

}

/*
 * What the browser asks for when a person opens a curve the deployment
 * publishes. The window names the curve's series and no tenor members, which
 * is what makes the read a published curve rather than a ladder assembled from
 * a tail per tenor.
 */
TEST_CASE("opening a published curve names its series and no members",
    "[curve][open]") {
    hedgr::testing::desk desk;
    auto input = desk.input();

    render::request_published_curve_window(input, desk.session,
        model::domain::published_curve{.label = "EUR-ESTR",
            .series_id = "s-curve",
            .points = 7});

    const auto asked = desk.commands.drain();
    REQUIRE(asked.size() == 1);
    const auto& request = asked.front();
    CHECK(request.kind == model::command::command_kind::open_window);
    CHECK(request.new_window_kind == model::domain::window_kind::term_structure);
    CHECK(request.instance_id == "i");
    CHECK(request.account_id == "a");
    CHECK(request.series_id == "s-curve");
    CHECK(request.series_label == "EUR-ESTR");
    CHECK(request.curve_key == "s-curve");
    CHECK(request.curve_label == "EUR-ESTR");
    CHECK(request.member_ids.empty());
    CHECK(request.member_tenors.empty());
}
