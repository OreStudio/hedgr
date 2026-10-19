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
#include "allocation_counter.hpp"
#include "hedgr.ui/render/chart_plot.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <deque>

namespace {

using hedgr::model::domain::market_point;
using hedgr::ui::render::chart_plot;
using hedgr::ui::render::chart_plot_key;
using hedgr::ui::render::update_chart_plot;

constexpr std::int64_t second = 1'000'000'000;

/** @brief One point a second from @p from to @p to, valued at its second. */
std::deque<market_point> seconds(int from, int to) {
    std::deque<market_point> points;
    for (int s = from; s <= to; ++s)
        points.push_back(market_point{.at_ns = s * second,
            .value = static_cast<double>(s)});
    return points;
}

}

TEST_CASE("a chart plot holds the points of its span", "[chart]") {
    const auto points = seconds(1, 10);
    chart_plot plot;
    REQUIRE(update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 5 * second}, points));
    CHECK(plot.start == 4);
    CHECK(plot.xs == std::vector<double>{5, 6, 7, 8, 9, 10});
    CHECK(plot.ys == std::vector<double>{5, 6, 7, 8, 9, 10});
    CHECK(plot.low == 5.0);
    CHECK(plot.high == 10.0);
    CHECK(plot.candles.empty());
    CHECK(plot.ema.empty());
}

TEST_CASE("a chart plot keeps a sparse series whole", "[chart]") {
    std::deque<market_point> points{{.at_ns = 1 * second, .value = 1.0},
        {.at_ns = 2 * second, .value = 2.0},
        {.at_ns = 100 * second, .value = 3.0}};
    chart_plot plot;
    update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 5 * second}, points);
    CHECK(plot.start == 0);
    CHECK(plot.ys.size() == 3);
}

TEST_CASE("a chart plot over all points sizes candles from what is in view",
    "[chart]") {
    /* Ten minutes in view reads best at fifteen second candles. */
    const auto points = seconds(0, 599);
    chart_plot plot;
    update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 0, .candles = true, .ema = true},
        points);
    CHECK(plot.start == 0);
    CHECK(plot.candle_width_ns == 15 * second);
    REQUIRE(plot.candles.size() == 40);
    CHECK(plot.candles.front().at_ns == 0);
    CHECK(plot.candles.front().open == 0.0);
    CHECK(plot.candles.front().close == 14.0);
    CHECK(plot.candles.front().high == 14.0);
    CHECK(plot.candles.front().low == 0.0);
    CHECK(plot.ema.size() == plot.ys.size());
}

TEST_CASE("a chart plot is rebuilt only when its key changes", "[chart][cost]") {
    auto points = seconds(1, 3600);
    chart_plot plot;
    const chart_plot_key key{.version = 7, .span_ns = 3600 * second,
        .candles = true, .ema = true};
    REQUIRE(update_chart_plot(plot, key, points));

    /* A frame with nothing new does no work and touches no allocator. */
    const auto before = hedgr::testing::allocations();
    for (int frame = 0; frame < 600; ++frame)
        CHECK_FALSE(update_chart_plot(plot, key, points));
    CHECK(hedgr::testing::allocations() - before == 0);

    /* A new tick is a new version; the rebuild reuses the storage it has. */
    points.pop_front();
    points.push_back(market_point{.at_ns = 3601 * second, .value = 0.5});
    auto next = key;
    next.version = 8;
    const auto rebuild = hedgr::testing::allocations();
    REQUIRE(update_chart_plot(plot, next, points));
    CHECK(hedgr::testing::allocations() - rebuild == 0);
    CHECK(plot.ys.back() == 0.5);
    CHECK(plot.low == 0.5);

    next.ema = false;
    REQUIRE(update_chart_plot(plot, next, points));
    CHECK(plot.ema.empty());
}

TEST_CASE("the cost of a chart frame over one hour", "[!benchmark][chart]") {
    const auto points = seconds(1, 3600);
    const chart_plot_key key{.version = 1, .span_ns = 3600 * second,
        .candles = true, .ema = true};

    /* What every frame paid before the plot was kept: fresh arrays. */
    const auto before = hedgr::testing::allocations();
    {
        chart_plot fresh;
        update_chart_plot(fresh, key, points);
    }
    WARN("allocations of one rebuild into fresh arrays: "
        << hedgr::testing::allocations() - before);

    BENCHMARK("rebuild into fresh arrays") {
        chart_plot fresh;
        update_chart_plot(fresh, key, points);
        return fresh.ys.size();
    };
    chart_plot kept;
    update_chart_plot(kept, key, points);
    BENCHMARK("frame with nothing new") {
        return update_chart_plot(kept, key, points);
    };
}
