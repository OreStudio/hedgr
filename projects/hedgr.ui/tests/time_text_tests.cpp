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
#include "hedgr.ui/render/time_text.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>

TEST_CASE("an as-of instant is ISO 8601 in UTC", "[time]") {
    using hedgr::ui::render::as_of_text;
    CHECK(as_of_text(1'792'360'345'000'000'000) == "2026-10-18T21:52:25Z");
    CHECK(as_of_text(0).empty());
}

TEST_CASE("an age reads as OreStudio words it", "[time]") {
    using hedgr::ui::render::age_text;
    CHECK(age_text(3'000'000'000) == "just now");
    CHECK(age_text(12'000'000'000) == "12 seconds ago");
    CHECK(age_text(5 * 60'000'000'000LL) == "5 minutes ago");
    CHECK(age_text(3612 * 1'000'000'000LL) == "1 hour ago");
    CHECK(age_text(2 * 86'400'000'000'000LL) == "2 days ago");
}

TEST_CASE("a tick's age is short and capped at ten minutes", "[time]") {
    using hedgr::ui::render::tick_age_text;
    constexpr std::int64_t second = 1'000'000'000;
    CHECK(tick_age_text(0) == "0s");
    CHECK(tick_age_text(12 * second) == "12s");
    CHECK(tick_age_text(59 * second) == "59s");
    CHECK(tick_age_text(60 * second) == "1m");
    CHECK(tick_age_text(9 * 60 * second + 59 * second) == "9m");
    CHECK(tick_age_text(10 * 60 * second) == "10m");
    CHECK(tick_age_text(10 * 60 * second + second) == ">10m");
    CHECK(tick_age_text(71 * 60 * second) == ">10m");
    CHECK(tick_age_text(-second) == "ahead");
}

TEST_CASE("an as-of instant carries its age in front", "[time]") {
    using hedgr::ui::render::as_of_age_text;
    constexpr std::int64_t second = 1'000'000'000;
    constexpr std::int64_t at = 1'792'360'345 * second;
    CHECK(as_of_age_text(at, at + 12 * second)
        == "12s \u00b7 2026-10-18T21:52:25Z");
    CHECK(as_of_age_text(at, at + 71 * 60 * second)
        == ">10m \u00b7 2026-10-18T21:52:25Z");
    CHECK(as_of_age_text(at, at - 90 * second)
        == "ahead \u00b7 2026-10-18T21:52:25Z");
    CHECK(as_of_age_text(0, at).empty());
    CHECK(as_of_age_text(at, 0) == "2026-10-18T21:52:25Z");
}
