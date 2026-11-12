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
#include "hedgr.runtime/worker/curve_worker.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

using hedgr::runtime::worker::build_curve;
using hedgr::runtime::worker::pillar_history;

TEST_CASE("every curve covers every pillar that has data", "[term_curve]") {
    /*
     * The 1M and 3M pillars were fetched from t=1; 6M and 12M only began at
     * t=3. A curve at t=1 or t=2 would hold two of four tenors.
     */
    const pillar_history by_term{
        {"1M", {{1, 1.0}, {2, 1.1}, {3, 1.2}, {4, 1.3}}},
        {"3M", {{1, 2.0}, {2, 2.1}, {3, 2.2}, {4, 2.3}}},
        {"6M", {{3, 3.2}, {4, 3.3}}},
        {"12M", {{3, 4.2}, {4, 4.3}}}};

    const auto curve = build_curve(by_term, 20);

    CHECK(curve.pillars == std::vector<std::string>{"1M", "3M", "6M", "12M"});
    CHECK(curve.as_of_ns == std::vector<std::int64_t>{3, 4});
    CHECK(curve.values.size() == 8);
    CHECK(curve.values[0] == 1.2);
    CHECK(curve.values[7] == 4.3);
}

TEST_CASE("a pillar's value is carried to a later instant", "[term_curve]") {
    const pillar_history by_term{
        {"1M", {{1, 1.0}, {3, 1.5}}},
        {"3M", {{1, 2.0}, {2, 2.5}}}};

    const auto curve = build_curve(by_term, 20);

    CHECK(curve.as_of_ns == std::vector<std::int64_t>{1, 2, 3});
    REQUIRE(curve.values.size() == 6);
    /* The 3M pillar's 2.5 is carried into the third instant. */
    CHECK(curve.values[4] == 1.5);
    CHECK(curve.values[5] == 2.5);
}

TEST_CASE("the newest curve survives when pillars start at different times",
    "[term_curve]") {
    const pillar_history by_term{
        {"1M", {{1, 1.0}}},
        {"30Y", {{9, 9.0}}}};

    const auto curve = build_curve(by_term, 20);

    CHECK(curve.as_of_ns == std::vector<std::int64_t>{9});
    CHECK(curve.pillars == std::vector<std::string>{"1M", "30Y"});
    /* 1M's value is carried nine steps forward to the only emitted instant. */
    CHECK(curve.values == std::vector<double>{1.0, 9.0});
}

TEST_CASE("history is bounded to the newest instants", "[term_curve]") {
    pillar_history by_term;
    for (std::int64_t at = 1; at <= 10; ++at)
        by_term["1M"][at] = static_cast<double>(at);

    const auto curve = build_curve(by_term, 4);

    CHECK(curve.as_of_ns == std::vector<std::int64_t>{7, 8, 9, 10});
    CHECK(curve.values == std::vector<double>{7.0, 8.0, 9.0, 10.0});
}

TEST_CASE("no observations make no curve", "[term_curve]") {
    CHECK(build_curve({}, 20).pillars.empty());
    CHECK(build_curve({}, 20).as_of_ns.empty());
}
