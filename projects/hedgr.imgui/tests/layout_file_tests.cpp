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
#include "hedgr.imgui/app/layout_file.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::imgui::app::layout_file_path;

TEST_CASE("the layout lives beside the store, not in the launch directory",
    "[layout]") {
    const std::filesystem::path store = "/srv/hedgr/data/hedgr.db";

    const auto layout = layout_file_path(store, "");

    CHECK(layout == std::filesystem::path("/srv/hedgr/data/imgui.ini"));
    CHECK(layout.is_absolute());
}

TEST_CASE("a named layout file wins over the store's", "[layout]") {
    const auto layout = layout_file_path("/srv/hedgr/data/hedgr.db",
        "/tmp/capture/imgui.ini");

    CHECK(layout == std::filesystem::path("/tmp/capture/imgui.ini"));
}

TEST_CASE("desktop and main-window positions never share a layout file",
    "[layout]") {
    const std::filesystem::path store = "/srv/hedgr/data/hedgr.db";

    const auto main_window = layout_file_path(store, "", false);
    const auto desktop = layout_file_path(store, "", true);

    CHECK(main_window == std::filesystem::path("/srv/hedgr/data/imgui.ini"));
    CHECK(desktop == std::filesystem::path("/srv/hedgr/data/imgui-desktop.ini"));
    CHECK(desktop != main_window);
}

TEST_CASE("a named layout file wins on either platform", "[layout]") {
    CHECK(layout_file_path("/srv/hedgr/data/hedgr.db", "/tmp/c/imgui.ini", true)
        == std::filesystem::path("/tmp/c/imgui.ini"));
}
