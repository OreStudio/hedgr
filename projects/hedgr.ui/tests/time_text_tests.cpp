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

TEST_CASE("an as-of instant is ISO 8601 in UTC", "[time]") {
    using hedgr::ui::render::as_of_text;
    using hedgr::ui::render::parse_iso8601_utc;

    const auto instant = parse_iso8601_utc("2026-10-18T21:52:25Z");
    REQUIRE(instant > 0);
    CHECK(as_of_text(instant) == "2026-10-18T21:52:25Z");

    CHECK(as_of_text(0).empty());
    CHECK(parse_iso8601_utc("") == 0);
    CHECK(parse_iso8601_utc("not a time") == 0);
}

TEST_CASE("an as-of instant reads in OreStudio's wire shape", "[time]") {
    using hedgr::ui::render::parse_iso8601_utc;

    const auto t_form = parse_iso8601_utc("2026-10-18T21:52:25Z");
    REQUIRE(t_form > 0);

    /* The shape to_iso8601_utc writes: a space and six fraction digits. */
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25.250000Z")
        == t_form + 250'000'000);
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25+00:00") == t_form);

    /* An offset or a missing designator is not misread as UTC. */
    CHECK(parse_iso8601_utc("2026-10-18T22:52:25+01:00") == 0);
    CHECK(parse_iso8601_utc("2026-10-18T21:52:25") == 0);
}

TEST_CASE("an age reads as OreStudio words it", "[time]") {
    using hedgr::ui::render::age_text;
    CHECK(age_text(3'000'000'000) == "just now");
    CHECK(age_text(12'000'000'000) == "12 seconds ago");
    CHECK(age_text(5 * 60'000'000'000LL) == "5 minutes ago");
    CHECK(age_text(3612 * 1'000'000'000LL) == "1 hour ago");
    CHECK(age_text(2 * 86'400'000'000'000LL) == "2 days ago");
}
