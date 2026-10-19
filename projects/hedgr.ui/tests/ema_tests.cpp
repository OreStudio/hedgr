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
#include "hedgr.ui/render/ema.hpp"
#include <catch2/catch_all.hpp>
#include <vector>

TEST_CASE("the ema is seeded with the first value", "[ema]") {
    std::vector<double> values{9.0, 9.0, 9.0, 9.0};
    hedgr::ui::render::exponential_moving_average({1.0, 3.0, 3.0}, values, 3);
    REQUIRE(values.size() == 3);
    CHECK(values[0] == 1.0);
    CHECK(values[1] == 2.0);
    CHECK(values[2] == 2.5);
}

TEST_CASE("the ema handles empty and single series", "[ema]") {
    std::vector<double> none{1.0};
    hedgr::ui::render::exponential_moving_average({}, none);
    CHECK(none.empty());
    std::vector<double> one;
    hedgr::ui::render::exponential_moving_average({4.0}, one);
    REQUIRE(one.size() == 1);
    CHECK(one.front() == 4.0);
}
