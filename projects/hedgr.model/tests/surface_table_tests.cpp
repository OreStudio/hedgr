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
#include "hedgr.model/state/surface_table.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using hedgr::model::state::surface_state;
using hedgr::model::state::surface_table;

namespace {

/** @brief The one grid the tests store: three expiries by two strikes. */
std::vector<std::string> expiries() { return {"1Y", "1W", "1M"}; }
std::vector<std::string> strikes() { return {"DEL/Spot/Put/25", "ATM"}; }

/**
 * @brief The values in the order the deployment states them.
 *
 * Row-major against @c expiries as given - 1Y, 1W, 1M - so the stored order
 * can be told apart from the stated one: a 0.10 on the first row is 1Y's, and
 * after the table sorts the rows by tenor it is the last row's.
 */
std::vector<double> stated_values() {
    return {0.10, 0.11, 0.20, 0.21, 0.30, 0.31};
}

}

TEST_CASE("a surface is interned once and shared by every window",
    "[surfaces]") {
    surface_table surfaces;
    const auto first = surfaces.intern("i", "a", "blueprint-1");
    const auto again = surfaces.intern("i", "a", "blueprint-1");
    CHECK(first == again);
    CHECK(surfaces.size() == 1);
    CHECK(surfaces.find("i", "a", "blueprint-1").has_value());
    CHECK_FALSE(surfaces.find("i", "a", "blueprint-2").has_value());
    /* Another session's surface of the same blueprint is another row. */
    CHECK_FALSE(surfaces.find("i", "b", "blueprint-1").has_value());
}

TEST_CASE("a surface sorts its expiries and keeps its strike order",
    "[surfaces]") {
    surface_table surfaces;
    const auto handle = surfaces.intern("i", "a", "blueprint-1");
    surfaces.begin(handle);
    REQUIRE(surfaces.get(handle) != nullptr);
    CHECK(surfaces.get(handle)->state == surface_state::reading);

    surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD", 1'700'000'000'000'000'000,
        expiries(), strikes(), stated_values());

    const auto* row = surfaces.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == surface_state::ready);
    REQUIRE(row->complete());
    CHECK(row->rows == 3);
    CHECK(row->columns == 2);

    /*
     * The expiries come back in tenor order - 1W, 1M, 1Y - and the values moved
     * with them, so a row's value is still the value of the expiry its label
     * names. This is what lets a plot draw the rows without sorting per frame.
     */
    const auto& rows = surfaces.axis(row->expiry_axis);
    REQUIRE(rows.pillars.size() == 3);
    CHECK(rows.pillars[0] == "1W");
    CHECK(rows.pillars[1] == "1M");
    CHECK(rows.pillars[2] == "1Y");
    CHECK(row->at(0, 0) == 0.20);
    CHECK(row->at(0, 1) == 0.21);
    CHECK(row->at(1, 0) == 0.30);
    CHECK(row->at(2, 0) == 0.10);

    /*
     * A delta ladder runs put to call, so its columns keep the deployment's
     * order: sorting their names alphabetically would draw the smile inside
     * out.
     */
    const auto& columns = surfaces.axis(row->strike_axis);
    REQUIRE(columns.pillars.size() == 2);
    CHECK(columns.pillars[0] == "DEL/Spot/Put/25");
    CHECK(columns.pillars[1] == "ATM");
}

TEST_CASE("a surface read with the wrong number of values holds no grid",
    "[surfaces]") {
    surface_table surfaces;
    const auto handle = surfaces.intern("i", "a", "blueprint-1");
    surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD", 1, expiries(), strikes(),
        {0.10, 0.11});

    const auto* row = surfaces.get(handle);
    REQUIRE(row != nullptr);
    /*
     * The shape is stated by the axes even when the values do not fill it, so a
     * broken answer is a shape with no grid rather than a grid with the wrong
     * shape - which would put one row's values under another row's label.
     */
    CHECK_FALSE(row->complete());
    CHECK(row->at(0, 0) == 0.0);
}

TEST_CASE("a failed surface keeps what it held and says why", "[surfaces]") {
    surface_table surfaces;
    const auto handle = surfaces.intern("i", "a", "blueprint-1");
    surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD", 1, expiries(), strikes(),
        stated_values());
    const auto held_version = surfaces.version(handle);

    surfaces.fail(handle, "no surface is published at or before this instant");

    const auto* row = surfaces.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == surface_state::failed);
    CHECK(row->error
        == "no surface is published at or before this instant");
    /* The grid is still there: a failed refresh does not erase a good read. */
    CHECK(row->complete());
    CHECK(row->at(1, 0) == 0.30);
    CHECK(surfaces.version(handle) > held_version);
}

TEST_CASE("a surface grid follows the axes it shares", "[surfaces]") {
    surface_table surfaces;
    const auto first = surfaces.intern("i", "a", "blueprint-1");
    const auto second = surfaces.intern("i", "a", "blueprint-2");
    surfaces.apply(first, "EURUSD_SSVI", "EUR", "USD", 1, expiries(), strikes(),
        stated_values());
    surfaces.apply(second, "GBPUSD_SSVI", "GBP", "USD", 1, expiries(), strikes(),
        stated_values());

    /* Two surfaces on the same grid share both axes, not four of them. */
    CHECK(surfaces.axes() == 2);

    surfaces.clear();
    CHECK(surfaces.size() == 0);
    CHECK(surfaces.axes() == 0);
    CHECK_FALSE(surfaces.find("i", "a", "blueprint-1").has_value());
}
