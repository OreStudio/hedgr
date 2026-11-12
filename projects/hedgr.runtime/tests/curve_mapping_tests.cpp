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
#include "hedgr.runtime/service/curve_mapping.hpp"
#include "hedgr.model/event/event.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

namespace service = hedgr::runtime::service;
namespace state = hedgr::model::state;
namespace transport = hedgr::session::transport;

namespace {

[[nodiscard]] transport::curve_point point(std::string pillar,
    double value, std::string kind = "discount") {
    transport::curve_point p;
    p.pillar = std::move(pillar);
    p.value = value;
    p.numeric = true;
    p.value_kind = std::move(kind);
    return p;
}

[[nodiscard]] transport::curve_instant set_of(std::int64_t as_of_ns,
    std::vector<transport::curve_point> points) {
    transport::curve_instant set;
    set.label = "EUR IR SWAP";
    set.series_id = "s-1";
    set.as_of_ns = as_of_ns;
    set.state_code = "published";
    set.points = std::move(points);
    return set;
}

}

TEST_CASE("a published curve becomes pillars and numbers", "[curves]") {
    const std::vector<transport::curve_instant> sets{set_of(100,
        {point("10Y", 0.075807871141227612),
            point("12M", 0.68910259588050338),
            point("2Y", 0.49232515532723253)})};

    const auto reading = service::read_published_curves(sets);
    CHECK(reading.label == "EUR IR SWAP");
    CHECK(reading.pillars == std::vector<std::string>{"10Y", "12M", "2Y"});
    CHECK(reading.as_of_ns == std::vector<std::int64_t>{100});
    CHECK(reading.values.size() == 3);
    CHECK(reading.values[2] == 0.49232515532723253);
}

TEST_CASE("a pillar the deployment could not spell is left out", "[curves]") {
    auto bad = point("2Y", 0.02);
    bad.numeric = false;
    const std::vector<transport::curve_instant> sets{set_of(100,
        {point("1Y", 0.01), bad, point("", 0.03), point("3Y", 0.04)})};

    const auto reading = service::read_published_curves(sets);
    CHECK(reading.pillars == std::vector<std::string>{"1Y", "3Y"});
    CHECK(reading.values.size() == 2);
}

TEST_CASE("instants are put oldest first and a pillar is carried forward",
    "[curves]") {
    /*
     * The deployment publishes newest first and only the pillars it
     * republished. A curve is the object as it stood, so 1Y keeps the value
     * the earlier instant gave it, and 2Y is a gap until it exists.
     */
    const std::vector<transport::curve_instant> sets{
        set_of(200, {point("1Y", 0.02)}),
        set_of(100, {point("2Y", 0.03)}),
    };

    const auto reading = service::read_published_curves(sets);
    CHECK(reading.pillars == std::vector<std::string>{"2Y", "1Y"});
    CHECK(reading.as_of_ns == std::vector<std::int64_t>{100, 200});
    REQUIRE(reading.values.size() == 4);
    CHECK(reading.values[0] == 0.03);
    CHECK(std::isnan(reading.values[1]));
    CHECK(reading.values[2] == 0.03);
    CHECK(reading.values[3] == 0.02);
}

TEST_CASE("a curve event lands on the row it names", "[curves]") {
    hedgr::model::event::event event;
    event.kind = hedgr::model::event::event_kind::curve_sets;
    event.curve_label = "EUR IR SWAP";
    event.curve_pillars = {"10Y", "1Y", "2Y"};
    event.curve_as_of_ns = {100};
    event.curve_values = {0.03, 0.01, 0.02};

    state::curve_table curves;
    const auto handle = curves.intern("i", "a", "s-1");
    REQUIRE(service::apply_curve_event(curves, handle, event));

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == state::curve_state::ready);
    CHECK(curves.axis(row->axis).pillars
        == std::vector<std::string>{"1Y", "2Y", "10Y"});
    CHECK(row->values[0] == 0.01);
}

TEST_CASE("a failed curve read keeps what the row held", "[curves]") {
    state::curve_table curves;
    const auto handle = curves.intern("i", "a", "s-1");

    hedgr::model::event::event held;
    held.kind = hedgr::model::event::event_kind::curve_sets;
    held.curve_label = "EUR IR SWAP";
    held.curve_pillars = {"1Y"};
    held.curve_as_of_ns = {100};
    held.curve_values = {0.01};
    REQUIRE(service::apply_curve_event(curves, handle, held));

    hedgr::model::event::event event;
    event.kind = hedgr::model::event::event_kind::curve_sets;
    event.message = "the deployment refused the curve";
    CHECK_FALSE(service::apply_curve_event(curves, handle, event));

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == state::curve_state::failed);
    CHECK(row->values.size() == 1);
}

TEST_CASE("a curve with no usable point leaves the row alone", "[curves]") {
    const std::vector<transport::curve_instant> sets{set_of(100, {})};
    CHECK(service::read_published_curves(sets).pillars.empty());
}
