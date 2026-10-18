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
#include "hedgr.ui/render/data_health.hpp"
#include <catch2/catch_all.hpp>

using namespace std::chrono_literals;
using hedgr::ui::render::assess_data_health;
using hedgr::ui::render::data_health;

TEST_CASE("data health moves from live to delayed to stale", "[health]") {
    constexpr auto now = 100s;
    constexpr auto s = std::chrono::seconds{1};
    CHECK(assess_data_health((now - 1s).count() * 1'000'000'000,
              now.count() * 1'000'000'000)
              .state == data_health::live);
    CHECK(assess_data_health((now - 10s).count() * 1'000'000'000,
              now.count() * 1'000'000'000)
              .state == data_health::delayed);
    CHECK(assess_data_health((now - 60s).count() * 1'000'000'000,
              now.count() * 1'000'000'000)
              .state == data_health::stale);
}

TEST_CASE("a future observation is live and no observation is unavailable",
    "[health]") {
    CHECK(assess_data_health(200, 100).state == data_health::live);
    CHECK(assess_data_health(0, 100).state == data_health::unavailable);
}

TEST_CASE("an age reads in the largest useful unit", "[health]") {
    using hedgr::ui::render::age_text;
    CHECK(age_text(12'000'000'000) == "12s");
    CHECK(age_text(5 * 60'000'000'000LL) == "5m");
    CHECK(age_text(3 * 3600'000'000'000LL) == "3h");
    CHECK(age_text(2 * 86'400'000'000'000LL) == "2d");
}
