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
#include "hedgr.ui/widget/instances_panel.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include <catch2/catch_all.hpp>

namespace widget = hedgr::ui::widget;

TEST_CASE("the filter matches a name or an id, in either case", "[instances_panel]") {
    CHECK(widget::instance_matches("", "Alpha", "a-1"));
    CHECK(widget::instance_matches("alp", "Alpha", "a-1"));
    CHECK(widget::instance_matches("ALP", "Alpha", "a-1"));
    CHECK(widget::instance_matches("PHA", "alpha", "a-1"));
    CHECK(widget::instance_matches("A-1", "Alpha", "a-1"));
    CHECK(widget::instance_matches("prod", "Beta", "Prod-East"));
    CHECK_FALSE(widget::instance_matches("gamma", "Alpha", "a-1"));
    CHECK_FALSE(widget::instance_matches("alphabet", "Alpha", "a-1"));
    CHECK_FALSE(widget::instance_matches("x", "", ""));
}

TEST_CASE("a row holds as many tiles as fit with a gap between, at least one",
    "[instances_panel]") {
    /* Tiles 100 wide, 10 apart: n tiles take 110 n - 10. */
    CHECK(widget::tile_columns(100.0f, 100.0f, 10.0f) == 1);
    CHECK(widget::tile_columns(209.0f, 100.0f, 10.0f) == 1);
    CHECK(widget::tile_columns(210.0f, 100.0f, 10.0f) == 2);
    CHECK(widget::tile_columns(330.0f, 100.0f, 10.0f) == 3);
    CHECK(widget::tile_columns(660.0f, 100.0f, 10.0f) == 6);
    CHECK(widget::tile_columns(50.0f, 100.0f, 10.0f) == 1);
    CHECK(widget::tile_columns(0.0f, 100.0f, 10.0f) == 1);
}

TEST_CASE("tiles fill each row left to right and rows stack with the gap",
    "[instances_panel]") {
    const auto first = widget::tile_position(0, 3, 100.0f, 80.0f, 10.0f);
    CHECK(first.x == 0.0f);
    CHECK(first.y == 0.0f);
    const auto second = widget::tile_position(1, 3, 100.0f, 80.0f, 10.0f);
    CHECK(second.x == 110.0f);
    CHECK(second.y == 0.0f);
    const auto wrapped = widget::tile_position(3, 3, 100.0f, 80.0f, 10.0f);
    CHECK(wrapped.x == 0.0f);
    CHECK(wrapped.y == 90.0f);
    const auto later = widget::tile_position(7, 3, 100.0f, 80.0f, 10.0f);
    CHECK(later.x == 110.0f);
    CHECK(later.y == 180.0f);
}

TEST_CASE("the grid is as tall as its rows and their gaps", "[instances_panel]") {
    CHECK(widget::tile_grid_height(0, 3, 80.0f, 10.0f) == 0.0f);
    CHECK(widget::tile_grid_height(1, 3, 80.0f, 10.0f) == 80.0f);
    CHECK(widget::tile_grid_height(3, 3, 80.0f, 10.0f) == 80.0f);
    CHECK(widget::tile_grid_height(4, 3, 80.0f, 10.0f) == 170.0f);
    CHECK(widget::tile_grid_height(7, 3, 80.0f, 10.0f) == 260.0f);
}

TEST_CASE("a panel is flush with its label until it would pass the right edge",
    "[instances_panel]") {
    CHECK(widget::menu_panel_left(100.0f, 300.0f, 10.0f, 1000.0f) == 100.0f);
    CHECK(widget::menu_panel_left(900.0f, 300.0f, 10.0f, 1000.0f) == 700.0f);
    CHECK(widget::menu_panel_left(5.0f, 300.0f, 10.0f, 1000.0f) == 10.0f);
    CHECK(widget::menu_panel_left(100.0f, 2000.0f, 10.0f, 1000.0f) == 10.0f);
}

TEST_CASE("instances sort by name and then id, folding case", "[instances_panel]") {
    CHECK(widget::instance_less("Alpha", "a-1", "Beta", "b-2"));
    CHECK_FALSE(widget::instance_less("Beta", "b-2", "Alpha", "a-1"));
    CHECK(widget::instance_less("alpha", "a-1", "Beta", "b-2"));
    CHECK_FALSE(widget::instance_less("Alpha", "a-1", "alpha", "a-1"));

    /* The same name is broken by the id, so one order is always drawn. */
    CHECK(widget::instance_less("Alpha", "a-1", "Alpha", "a-2"));
    CHECK_FALSE(widget::instance_less("Alpha", "a-2", "Alpha", "a-1"));
    CHECK_FALSE(widget::instance_less("Alpha", "a-1", "Alpha", "a-1"));

    /* A prefix comes before the longer name, as a reader expects. */
    CHECK(widget::instance_less("Brit", "a-1", "Britain", "a-2"));
}
