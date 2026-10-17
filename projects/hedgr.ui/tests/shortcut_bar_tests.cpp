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
#include "hedgr.ui/render/shortcut_bar.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::ui::render::hover_is_new_entry;

/*
 * The tick is a change and nothing else, so these are the only three answers
 * the bar can ask for. A stationary pointer must stay silent or the bar would
 * buzz for as long as the pointer rested on it, which is the failure the whole
 * change exists to avoid; a pointer leaving must stay silent because otherwise
 * the tick would fire every time the pointer left the bar.
 */
TEST_CASE("a stationary hover is not a new entry", "[shortcut_bar]") {
    CHECK_FALSE(hover_is_new_entry("instance-a", "instance-a"));
}

TEST_CASE("moving between two entries is a new entry", "[shortcut_bar]") {
    CHECK(hover_is_new_entry("instance-a", "instance-b"));
}

TEST_CASE("leaving the bar is not a new entry", "[shortcut_bar]") {
    CHECK_FALSE(hover_is_new_entry("instance-a", ""));
    CHECK_FALSE(hover_is_new_entry("", ""));
}

TEST_CASE("approaching an entry is a new entry", "[shortcut_bar]") {
    CHECK(hover_is_new_entry("", "instance-a"));
}
