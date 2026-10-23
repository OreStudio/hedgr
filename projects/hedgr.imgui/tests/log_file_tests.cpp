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
#include "hedgr.imgui/app/log_file.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::imgui::app::resolve_log_target;

TEST_CASE("logging switched on with nothing else writes a file beside the store",
    "[log]") {
    const auto target = resolve_log_target({}, "/srv/hedgr/data/hedgr.db");

    CHECK(target.filename == "hedgr.imgui.log");
    CHECK(target.directory == std::filesystem::path("/srv/hedgr/data/log"));
}

TEST_CASE("a file or directory the caller named is kept", "[log]") {
    const auto named = resolve_log_target(
        {"mine.log", "/var/log/hedgr"}, "/srv/hedgr/data/hedgr.db");
    CHECK(named.filename == "mine.log");
    CHECK(named.directory == std::filesystem::path("/var/log/hedgr"));

    const auto dir_only = resolve_log_target({"", "../log"},
        "/srv/hedgr/data/hedgr.db");
    CHECK(dir_only.filename == "hedgr.imgui.log");
    CHECK(dir_only.directory == std::filesystem::path("../log"));
}
