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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/state/series_table.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>

/*
 * The plan is the whole decision of whether a chart span needs a deeper read,
 * so each test names one input that changes it and checks the exact depth.
 */

namespace {

namespace domain = hedgr::model::domain;
using domain::market_level;
using domain::plan_history_read;

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t minute = 60 * second;
constexpr std::int64_t hour = 60 * minute;
constexpr std::int64_t newest = 1'800'000'000 * second;

}

TEST_CASE("a span the level already holds needs no read", "[history]") {
    /* One hour on the fine level: the oldest bucket is two hours back. */
    CHECK_FALSE(plan_history_read(hour, newest - 2 * hour, newest, 0));
    /* A bucket inside the span's first bucket still covers it. */
    CHECK_FALSE(plan_history_read(hour, newest - hour + second, newest, 0));
}

TEST_CASE("a span the level does not hold asks back to the span's start",
    "[history]") {
    /* The first backfill held 33 minutes of one second data. */
    const auto plan = plan_history_read(hour, newest - 33 * minute, newest, 0);
    REQUIRE(plan);
    CHECK(plan->level == market_level::fine);
    CHECK(plan->from_ns == newest - hour);
    CHECK(plan->to_ns == newest - 33 * minute);
}

TEST_CASE("the level a span reads decides the depth it asks for", "[history]") {
    const auto day = plan_history_read(24 * hour, newest - hour, newest, 0);
    REQUIRE(day);
    CHECK(day->level == market_level::medium);
    CHECK(day->from_ns == newest - 24 * hour);

    const auto all = plan_history_read(0, newest - hour, newest, 0);
    REQUIRE(all);
    CHECK(all->level == market_level::coarse);
    CHECK(all->from_ns == newest - 720 * hour);
}

TEST_CASE("a span is never asked for beyond what its ring keeps", "[history]") {
    /* Three days on the coarse level would be fine; 30 days is the cap. */
    const auto plan = plan_history_read(90 * 24 * hour, newest - hour, newest, 0);
    REQUIRE(plan);
    CHECK(plan->from_ns == newest - 720 * hour);
    CHECK(newest - plan->from_ns
        <= static_cast<std::int64_t>(
               domain::market_level_capacity(plan->level))
            * domain::market_level_ns(plan->level));
}

TEST_CASE("an earlier read that went as far back is not asked again",
    "[history]") {
    const auto oldest = newest - 33 * minute;
    CHECK_FALSE(plan_history_read(hour, oldest, newest, newest - hour));
    CHECK_FALSE(plan_history_read(hour, oldest, newest, newest - 2 * hour));
    /* One that stopped short of the span's start leaves the ask open. */
    CHECK(plan_history_read(hour, oldest, newest, newest - 40 * minute));
}

TEST_CASE("a level with nothing in it has nothing to extend", "[history]") {
    CHECK_FALSE(plan_history_read(hour, 0, 0, 0));
}

TEST_CASE("a history answer is recorded on its row and stamps nothing",
    "[history][table]") {
    hedgr::model::state::series_table table;
    const auto h = table.intern("i", "a", "s");
    CHECK_FALSE(table.history(h).reading);
    CHECK(table.history(h).floor_ns == 0);

    const auto stamp = table.version(h);
    table.begin_history(h);
    CHECK(table.history(h).reading);
    table.finish_history(h, newest - hour, true);
    CHECK_FALSE(table.history(h).reading);
    CHECK(table.history(h).truncated);
    CHECK(table.history(h).floor_ns == newest - hour);
    CHECK(table.version(h) == stamp);

    /* A shallower answer does not forget a deeper ask. */
    table.finish_history(h, newest - 10 * minute, false);
    CHECK(table.history(h).floor_ns == newest - hour);
    CHECK_FALSE(table.history(h).truncated);

    table.begin_history(h);
    table.abandon_history(h);
    CHECK_FALSE(table.history(h).reading);
    CHECK(table.history(h).floor_ns == newest - hour);
}

TEST_CASE("a stale handle has no history", "[history][table]") {
    hedgr::model::state::series_table table;
    const auto h = table.intern("i", "a", "s");
    table.erase(h);
    table.begin_history(h);
    CHECK_FALSE(table.history(h).reading);
}
