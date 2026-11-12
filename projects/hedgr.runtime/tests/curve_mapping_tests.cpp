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
#include <chrono>
#include <string>

namespace service = hedgr::runtime::service;
namespace state = hedgr::model::state;
namespace messaging = ores::marketdata::messaging;

namespace {

[[nodiscard]] messaging::published_curve_point point(std::string pillar,
    std::string value, std::string kind = "discount") {
    messaging::published_curve_point p;
    p.pillar = std::move(pillar);
    p.value = std::move(value);
    p.value_kind = std::move(kind);
    return p;
}

[[nodiscard]] messaging::published_curve_set set_of(
    std::vector<messaging::published_curve_point> points) {
    messaging::published_curve_set set;
    set.curve = "EUR IR SWAP";
    set.series_id = "s-1";
    set.as_of = std::chrono::system_clock::time_point{
        std::chrono::seconds{1'700'000'000}};
    set.point_count = static_cast<std::uint32_t>(points.size());
    set.points = std::move(points);
    return set;
}

}

TEST_CASE("a published curve becomes pillars and numbers", "[curves]") {
    const auto set = set_of({point("10Y", "0.075807871141227612"),
        point("12M", "0.68910259588050338"),
        point("2Y", "0.49232515532723253")});

    const auto reading = service::read_published_curve(set);
    CHECK(reading.label == "EUR IR SWAP");
    CHECK(reading.pillars == std::vector<std::string>{"10Y", "12M", "2Y"});
    CHECK(reading.as_of_ns.size() == 1);
    CHECK(reading.values.size() == 3);
    CHECK(reading.values[2] == 0.49232515532723253);
}

TEST_CASE("a pillar whose value will not parse is left out", "[curves]") {
    const auto set = set_of({point("1Y", "0.01"), point("2Y", ""),
        point("", "0.02"), point("3Y", "not-a-number")});

    const auto reading = service::read_published_curve(set);
    CHECK(reading.pillars == std::vector<std::string>{"1Y"});
    CHECK(reading.values.size() == 1);
}

TEST_CASE("an applied curve lands on the row in drawing order", "[curves]") {
    state::curve_table curves;
    const auto handle = curves.intern("i", "a", "s-1");
    const auto set = set_of({point("10Y", "0.03"), point("1Y", "0.01"),
        point("2Y", "0.02")});

    REQUIRE(service::apply_published_curve(curves, handle, set));

    const auto* row = curves.get(handle);
    REQUIRE(row != nullptr);
    CHECK(row->state == state::curve_state::ready);
    CHECK(curves.axis(row->axis).pillars
        == std::vector<std::string>{"1Y", "2Y", "10Y"});
    CHECK(row->values[0] == 0.01);
    CHECK(row->values[2] == 0.03);
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
    REQUIRE(service::apply_published_curve(curves, handle,
        set_of({point("1Y", "0.01")})));

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
    state::curve_table curves;
    const auto handle = curves.intern("i", "a", "s-1");
    CHECK_FALSE(service::apply_published_curve(curves, handle, set_of({})));
    CHECK(curves.get(handle)->state == state::curve_state::unread);
}
