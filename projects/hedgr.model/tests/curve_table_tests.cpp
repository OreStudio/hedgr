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
#include "hedgr.model/state/curve_table.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using hedgr::model::state::curve_history_limit;
using hedgr::model::state::curve_state;
using hedgr::model::state::curve_table;

TEST_CASE("a curve is interned once and shared by every window", "[curves]") {
    curve_table curves;
    const auto first = curves.intern("i", "a", "s");
    const auto again = curves.intern("i", "a", "s");
    CHECK(first == again);
    CHECK(curves.size() == 1);
    CHECK(curves.find("i", "a", "s").has_value());
    CHECK_FALSE(curves.find("i", "a", "other").has_value());
}

TEST_CASE("a curve holds its instants as rows over one pillar order",
    "[curves]") {
    curve_table curves;
    const auto handle = curves.intern("i", "a", "s");
    curves.begin(handle);
    REQUIRE(curves.get(handle) != nullptr);
    CHECK(curves.get(handle)->state == curve_state::reading);

    curves.apply(handle, "EUR IR SWAP", {"1Y", "2Y", "10Y"}, {100, 200},
        {0.01, 0.02, 0.03, 0.011, 0.021, 0.031});

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == curve_state::ready);
    /* The pillar axis is shared, and the values arrive in its order. */
    const auto& axis = curves.axis(row->axis);
    CHECK(axis.pillars.size() == 3);
    CHECK(row->instants() == 2);
    CHECK(row->values.size() == 6);
    REQUIRE(row->instant(1, axis.pillars.size()) != nullptr);
    CHECK(row->instant(1, axis.pillars.size())[2] == 0.031);
    CHECK(row->instant(2, axis.pillars.size()) == nullptr);
}

TEST_CASE("pillars are ordered by tenor, and a stranger sorts last", "[curves]") {
    const std::vector<std::string> pillars{"10Y", "1M", "ON", "2Y", "1Y"};
    const auto order = curve_table::pillar_order(pillars);
    CHECK(pillars[order[0]] == "1M");
    CHECK(pillars[order[1]] == "1Y");
    CHECK(pillars[order[2]] == "2Y");
    CHECK(pillars[order[3]] == "10Y");
    CHECK(pillars[order[4]] == "ON");
}

TEST_CASE("curves on the same pillars share one axis, and one order",
    "[curves]") {
    curve_table curves;
    const auto eur = curves.intern("i", "a", "eur");
    const auto gbp = curves.intern("i", "a", "gbp");
    /* The deployment publishes its pillars in no particular order. */
    curves.apply(eur, "EUR", {"10Y", "1Y", "2Y"}, {100},
        {0.03, 0.01, 0.02});
    curves.apply(gbp, "GBP", {"2Y", "10Y", "1Y"}, {100},
        {0.025, 0.035, 0.015});

    REQUIRE(curves.get(eur) != nullptr);
    REQUIRE(curves.get(gbp) != nullptr);
    CHECK(curves.get(eur)->axis == curves.get(gbp)->axis);
    CHECK(curves.axes() == 1);
    const auto& axis = curves.axis(curves.get(eur)->axis);
    CHECK(axis.pillars == std::vector<std::string>{"1Y", "2Y", "10Y"});
    /* Both curves are stored in the axis order, so a plot needs no sort. */
    CHECK(curves.get(eur)->values[0] == 0.01);
    CHECK(curves.get(eur)->values[1] == 0.02);
    CHECK(curves.get(eur)->values[2] == 0.03);
    CHECK(curves.get(gbp)->values[0] == 0.015);
    CHECK(curves.get(gbp)->values[2] == 0.035);
}

TEST_CASE("a curve keeps only the newest instants it is given", "[curves]") {
    curve_table curves;
    const auto handle = curves.intern("i", "a", "s");
    const auto count = curve_history_limit + 5;
    std::vector<std::int64_t> as_of;
    std::vector<double> values;
    for (std::size_t i = 0; i < count; ++i) {
        as_of.push_back(static_cast<std::int64_t>(100 + i));
        values.push_back(static_cast<double>(i));
    }
    curves.apply(handle, "EUR", {"1Y"}, as_of, values);

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->instants() == curve_history_limit);
    /* The oldest five fell off, and the newest is still the newest. */
    CHECK(row->as_of_ns.front() == 105);
    CHECK(row->as_of_ns.back() == static_cast<std::int64_t>(100 + count - 1));
    CHECK(row->values.front() == 5.0);
}

TEST_CASE("a failed read keeps what the curve already held", "[curves]") {
    curve_table curves;
    const auto handle = curves.intern("i", "a", "s");
    curves.apply(handle, "EUR IR SWAP", {"1Y"}, {100}, {0.01});
    curves.begin(handle);
    curves.fail(handle, "the deployment refused the curve");

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == curve_state::failed);
    CHECK(row->error == "the deployment refused the curve");
    CHECK(curves.axis(row->axis).pillars.size() == 1);
    CHECK(row->values.size() == 1);
}
