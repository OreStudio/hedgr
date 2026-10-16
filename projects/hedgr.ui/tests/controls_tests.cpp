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
#include "hedgr.ui/widget/controls.hpp"
#include <catch2/catch_all.hpp>
#include <string>
#include <string_view>

/*
 * The selectable value itself cannot be exercised here, because drawing one
 * needs a frame and a frame needs a font atlas and a rendering backend. What is
 * tested instead is the path arithmetic the details pane shares with it, which
 * is the part a regression could hide in without any frame being involved.
 */

using hedgr::ui::widget::instance_picture_path;

TEST_CASE("a picture key resolves to the file beside the pictures", "[controls]") {
    REQUIRE(instance_picture_path("/opt/Hedgr/share/hedgr.ui/instances",
                "brave_hopper")
        == "/opt/Hedgr/share/hedgr.ui/instances/brave_hopper.png");
}

TEST_CASE("a directory with a trailing slash is not doubled", "[controls]") {
    REQUIRE(instance_picture_path("/opt/hedgr/instances/", "swift_curie")
        == "/opt/hedgr/instances/swift_curie.png");
}

TEST_CASE("an instance with no key has no file path", "[controls]") {
    /*
     * A blank key is a choice and not a missing picture, so it has to come back
     * as no path rather than as the directory with a bare extension, which a
     * reader would take for a file that exists.
     */
    REQUIRE(instance_picture_path("/opt/hedgr/instances", "").empty());
}

TEST_CASE("an install with no picture directory has no file path", "[controls]") {
    REQUIRE(instance_picture_path("", "brave_hopper").empty());
}
