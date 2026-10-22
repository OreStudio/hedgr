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
#include "hedgr.imgui/app/minimised_frames.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::imgui::app::skips_frame_while_minimised;

TEST_CASE("a minimised desk with nothing torn out draws nothing",
    "[minimised]") {
    CHECK(skips_frame_while_minimised(true, false, 1));
}

TEST_CASE("torn-out windows keep drawing while the main window is minimised",
    "[minimised]") {
    CHECK_FALSE(skips_frame_while_minimised(true, false, 2));
}

TEST_CASE("a visible main window and a capture never skip", "[minimised]") {
    CHECK_FALSE(skips_frame_while_minimised(false, false, 1));
    CHECK_FALSE(skips_frame_while_minimised(true, true, 1));
}
