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
#include "hedgr.model/domain/health.hpp"
#include "hedgr.model/domain/market.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using hedgr::model::domain::assess_data_health;
using hedgr::model::domain::assess_verdicts;
using hedgr::model::domain::data_health;
using hedgr::model::domain::fx_health_thresholds;

namespace {

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t now = 100 * second;

[[nodiscard]] data_health fx(std::int64_t as_of_ns) {
    return assess_data_health(as_of_ns, now, fx_health_thresholds);
}

}

TEST_CASE("data health moves from live to delayed to stale", "[health]") {
    CHECK(fx(now - 1 * second) == data_health::live);
    CHECK(fx(now - 10 * second) == data_health::delayed);
    CHECK(fx(now - 60 * second) == data_health::stale);
}

TEST_CASE("a future observation is live and no observation is unavailable",
    "[health]") {
    CHECK(fx(now + second) == data_health::live);
    CHECK(fx(0) == data_health::unavailable);
}

TEST_CASE("every market screen spells a freshness state the same way",
    "[health]") {
    using hedgr::model::domain::data_health_word;
    CHECK(data_health_word(data_health::live) == "LIVE");
    CHECK(data_health_word(data_health::delayed) == "DELAYED");
    CHECK(data_health_word(data_health::stale) == "STALE");
    CHECK(data_health_word(data_health::unavailable) == "NO DATA");
}

TEST_CASE("a series is as of its newest point at any level", "[health]") {
    using hedgr::model::domain::market_point;
    hedgr::model::domain::market_series_snapshot snapshot;
    CHECK(snapshot.newest_at_ns() == 0);

    snapshot.medium.push_back(market_point{.at_ns = now - 50 * second});
    snapshot.coarse.push_back(market_point{.at_ns = now - 3000 * second});
    snapshot.fine.push_back(market_point{.at_ns = now - 1 * second});
    CHECK(snapshot.newest_at_ns() == now - 1 * second);
    CHECK(fx(snapshot.newest_at_ns()) == data_health::live);

    snapshot.fine.clear();
    CHECK(snapshot.newest_at_ns() == now - 50 * second);
}

TEST_CASE("a lagging rate delays a batch and a stopped feed makes it stale",
    "[health]") {
    CHECK(assess_verdicts({.fresh = 3}) == data_health::live);
    CHECK(assess_verdicts({.fresh = 3, .stale = 1}) == data_health::delayed);
    CHECK(assess_verdicts({.stale = 2}) == data_health::delayed);
    CHECK(assess_verdicts({.fresh = 3, .unavailable = 1})
        == data_health::delayed);
    CHECK(assess_verdicts({.fresh = 3, .disconnected = 1})
        == data_health::stale);
    CHECK(assess_verdicts({.unavailable = 4}) == data_health::unavailable);
    CHECK(assess_verdicts({}) == data_health::unavailable);
}
