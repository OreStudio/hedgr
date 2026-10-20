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
#include "hedgr.runtime/worker/term_structure_worker.hpp"
#include <catch2/catch_test_macros.hpp>

using hedgr::runtime::worker::build_term_curves;
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

    const auto curves = build_term_curves(by_term, 20);

    REQUIRE(curves.size() == 2);
    CHECK(curves.front().at_ns == 3);
    CHECK(curves.back().at_ns == 4);
    for (const auto& curve : curves)
        CHECK(curve.nodes.size() == 4);
}

TEST_CASE("a pillar's value is carried to a later instant", "[term_curve]") {
    const pillar_history by_term{
        {"1M", {{1, 1.0}, {3, 1.5}}},
        {"3M", {{1, 2.0}, {2, 2.5}}}};

    const auto curves = build_term_curves(by_term, 20);

    REQUIRE(curves.size() == 3);
    const auto& last = curves.back();
    REQUIRE(last.at_ns == 3);
    REQUIRE(last.nodes.size() == 2);
    CHECK(last.nodes[0].tenor == "1M");
    CHECK(last.nodes[0].value == 1.5);
    CHECK(last.nodes[1].tenor == "3M");
    CHECK(last.nodes[1].value == 2.5);
}

TEST_CASE("the newest curve survives when pillars start at different times",
    "[term_curve]") {
    const pillar_history by_term{
        {"1M", {{1, 1.0}}},
        {"30Y", {{9, 9.0}}}};

    const auto curves = build_term_curves(by_term, 20);

    REQUIRE(curves.size() == 1);
    CHECK(curves.front().at_ns == 9);
    CHECK(curves.front().nodes.size() == 2);
}

TEST_CASE("history is bounded to the newest instants", "[term_curve]") {
    pillar_history by_term;
    for (std::int64_t at = 1; at <= 10; ++at)
        by_term["1M"][at] = static_cast<double>(at);

    const auto curves = build_term_curves(by_term, 4);

    REQUIRE(curves.size() == 4);
    CHECK(curves.front().at_ns == 7);
    CHECK(curves.back().at_ns == 10);
}

TEST_CASE("no observations make no curves", "[term_curve]") {
    CHECK(build_term_curves({}, 20).empty());
}
