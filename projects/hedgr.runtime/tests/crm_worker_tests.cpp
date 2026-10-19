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
#include "hedgr.runtime/worker/crm_worker.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::runtime::worker::parse_iso8601_utc;

TEST_CASE("a CRM as-of reads in OreStudio's wire shape", "[crm]") {
    constexpr std::int64_t t_form = 1'792'360'345'000'000'000;
    CHECK(parse_iso8601_utc("2026-10-18T21:52:25Z") == t_form);

    /* The shape to_iso8601_utc writes: a space and six fraction digits. */
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25.250000Z")
        == t_form + 250'000'000);
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25+00:00") == t_form);

    /* An offset or a missing designator is not misread as UTC. */
    CHECK(parse_iso8601_utc("2026-10-18T22:52:25+01:00") == 0);
    CHECK(parse_iso8601_utc("2026-10-18T21:52:25") == 0);
    CHECK(parse_iso8601_utc("") == 0);
    CHECK(parse_iso8601_utc("not a time") == 0);
}
