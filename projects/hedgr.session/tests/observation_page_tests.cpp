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
#include "hedgr.session/transport/observation_page.hpp"
#include <algorithm>
#include <boost/uuid/random_generator.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace transport = hedgr::session::transport;
using ores::marketdata::domain::market_observation;
using ores::marketdata::messaging::list_by_series_id_market_observations_request;

namespace {

/*
 * The by-series read as the deployment answers it: with no order named it
 * sorts by observation time, newest first, then pages. Ids are random, so a
 * read ordered by id would return an arbitrary slice of the history.
 */
std::vector<market_observation> answer(
    const list_by_series_id_market_observations_request& request,
    std::vector<market_observation> rows) {
    REQUIRE(request.order.field.empty());
    std::ranges::sort(rows, std::ranges::greater{},
        &market_observation::observation_datetime);
    const auto begin = std::min<std::size_t>(request.offset, rows.size());
    const auto end = std::min<std::size_t>(begin + request.limit, rows.size());
    return {rows.begin() + static_cast<std::ptrdiff_t>(begin),
        rows.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::chrono::system_clock::time_point second(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::seconds(n);
}

}

TEST_CASE("a series longer than the window backfills its newest points",
    "[transport]") {
    boost::uuids::random_generator ids;
    const auto series = ids();
    std::vector<market_observation> history;
    for (int i = 0; i < 3000; ++i) {
        market_observation row;
        row.id = ids();
        row.series_id = series;
        row.observation_datetime = second(i);
        row.value = std::to_string(i);
        history.push_back(std::move(row));
    }
    std::ranges::sort(history, {}, &market_observation::id);

    const auto request = transport::newest_observations_request(series, 2000);
    REQUIRE(request.series_id == series);
    const auto page = answer(request, history);
    const auto read = transport::series_read_from(page);

    REQUIRE(read.points.size() == 2000);
    for (std::size_t i = 0; i < read.points.size(); ++i)
        REQUIRE(read.points[i].value == static_cast<double>(1000 + i));
    REQUIRE(read.points.front().at_ns
        == std::chrono::nanoseconds(std::chrono::seconds(1000)).count());
}

TEST_CASE("an observation that is not a number is not plotted",
    "[transport]") {
    std::vector<market_observation> rows(2);
    rows[0].observation_datetime = second(2);
    rows[0].value = "n/a";
    rows[1].observation_datetime = second(1);
    rows[1].value = "1.5";

    const auto read = transport::series_read_from(rows);

    REQUIRE(read.points.size() == 1);
    REQUIRE(read.points[0].value == 1.5);
}
