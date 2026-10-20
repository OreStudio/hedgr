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
#include "hedgr.model/state/series_ring.hpp"
#include "hedgr.ui/render/chart_plot.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>

namespace {

using hedgr::model::state::series_columns;
using hedgr::model::state::series_point;
using hedgr::model::state::series_ring;
using hedgr::ui::render::chart_plot;
using hedgr::ui::render::chart_plot_key;
using hedgr::ui::render::chart_plot_start;
using hedgr::ui::render::update_chart_plot;

constexpr std::int64_t second = 1'000'000'000;

/** @brief A ring holding one point a second from @p from to @p to, valued at its second. */
series_ring seconds(int from, int to, std::size_t capacity = 7200) {
    series_ring ring{capacity};
    for (int s = from; s <= to; ++s)
        ring.append(series_point{.at_ns = s * second,
            .value = static_cast<double>(s)});
    return ring;
}

/** @brief The values of the points a plot has in view, read from the columns. */
std::vector<double> in_view(const chart_plot& plot, const series_columns& columns,
    const double* series_columns::*column) {
    const auto start = chart_plot_start(plot, columns);
    return {(columns.*column) + start, (columns.*column) + columns.size};
}

}

TEST_CASE("a chart plot holds the points of its span", "[chart]") {
    const auto ring = seconds(1, 10);
    chart_plot plot;
    REQUIRE(update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 5 * second}, ring.columns()));
    const auto columns = ring.columns();
    CHECK(chart_plot_start(plot, columns) == 4);
    CHECK(in_view(plot, columns, &series_columns::at_s)
        == std::vector<double>{5, 6, 7, 8, 9, 10});
    CHECK(in_view(plot, columns, &series_columns::value)
        == std::vector<double>{5, 6, 7, 8, 9, 10});
    CHECK(plot.low == 5.0);
    CHECK(plot.high == 10.0);
    CHECK(plot.candles.empty());
    CHECK(plot.ema.empty());
}

TEST_CASE("a chart plot keeps a sparse series whole", "[chart]") {
    series_ring ring{7200};
    ring.append({.at_ns = 1 * second, .value = 1.0});
    ring.append({.at_ns = 2 * second, .value = 2.0});
    ring.append({.at_ns = 100 * second, .value = 3.0});
    chart_plot plot;
    update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 5 * second}, ring.columns());
    CHECK(chart_plot_start(plot, ring.columns()) == 0);
    CHECK(in_view(plot, ring.columns(), &series_columns::value).size() == 3);
}

TEST_CASE("a chart plot over all points sizes candles from what is in view",
    "[chart]") {
    /* Ten minutes in view reads best at fifteen second candles. */
    const auto ring = seconds(0, 599);
    chart_plot plot;
    update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 0, .candles = true, .ema = true},
        ring.columns());
    CHECK(chart_plot_start(plot, ring.columns()) == 0);
    CHECK(plot.candle_width_ns == 15 * second);
    REQUIRE(plot.candles.size() == 40);
    CHECK(plot.candles.front().at_ns == 0);
    CHECK(plot.candles.front().open == 0.0);
    CHECK(plot.candles.front().close == 14.0);
    CHECK(plot.candles.front().high == 14.0);
    CHECK(plot.candles.front().low == 0.0);
    CHECK(plot.ema.size() == ring.columns().size);
}

TEST_CASE("a chart plot is rebuilt only when its key changes", "[chart][cost]") {
    auto ring = seconds(1, 3600, 3600);
    chart_plot plot;
    const chart_plot_key key{.version = 7, .span_ns = 3600 * second,
        .candles = true, .ema = true};
    REQUIRE(update_chart_plot(plot, key, ring.columns()));

    /* A frame with nothing new does no work and touches no allocator. */
    const auto before = hedgr::testing::allocations();
    for (int frame = 0; frame < 600; ++frame)
        CHECK_FALSE(update_chart_plot(plot, key, ring.columns()));
    CHECK(hedgr::testing::allocations() - before == 0);

    /* A new tick is a new version; the rebuild reuses the storage it has. */
    ring.append({.at_ns = 3601 * second, .value = 0.5});
    auto next = key;
    next.version = 8;
    const auto rebuild = hedgr::testing::allocations();
    REQUIRE(update_chart_plot(plot, next, ring.columns()));
    CHECK(hedgr::testing::allocations() - rebuild == 0);
    CHECK(ring.columns().value[ring.columns().size - 1] == 0.5);
    CHECK(plot.low == 0.5);

    next.ema = false;
    REQUIRE(update_chart_plot(plot, next, ring.columns()));
    CHECK(plot.ema.empty());
}

TEST_CASE("a chart plot still starts at its point after the ring compacts",
    "[chart]") {
    /* A buffer of 125 for a capacity of 100: the 126th point moves the rest. */
    series_ring ring{100};
    for (int s = 1; s <= 100; ++s)
        ring.append({.at_ns = s * second, .value = static_cast<double>(s)});
    chart_plot plot;
    const chart_plot_key key{.version = 1, .span_ns = 10 * second};
    REQUIRE(update_chart_plot(plot, key, ring.columns()));
    const auto first = ring.columns();
    REQUIRE(first.first_seq == 0);
    REQUIRE(first.value[chart_plot_start(plot, first)] == 90.0);

    for (int s = 101; s <= 140; ++s)
        ring.append({.at_ns = s * second, .value = static_cast<double>(s)});
    const auto moved = ring.columns();
    REQUIRE(moved.first_seq > 0);

    /* Nothing asked for a rebuild, so the plot keeps its sequence number. */
    CHECK_FALSE(update_chart_plot(plot, key, moved));
    const auto start = chart_plot_start(plot, moved);
    CHECK(start == 89 - moved.first_seq);
    CHECK(moved.value[start] == 90.0);
    CHECK(moved.at_ns[start] == 90 * second);
}

TEST_CASE("a chart plot whose first point left the ring starts at the oldest",
    "[chart]") {
    series_ring ring{100};
    for (int s = 1; s <= 50; ++s)
        ring.append({.at_ns = s * second, .value = static_cast<double>(s)});
    chart_plot plot;
    update_chart_plot(plot, chart_plot_key{.version = 1, .span_ns = 10 * second},
        ring.columns());
    for (int s = 51; s <= 400; ++s)
        ring.append({.at_ns = s * second, .value = static_cast<double>(s)});
    const auto columns = ring.columns();
    REQUIRE(columns.first_seq > 40);
    CHECK(chart_plot_start(plot, columns) == 0);
    CHECK(chart_plot_start(chart_plot{}, series_columns{}) == 0);
}

TEST_CASE("a chart plot copies no points", "[chart][cost]") {
    const auto ring = seconds(1, 3600, 3600);
    chart_plot plot;
    const auto before = hedgr::testing::allocations();
    REQUIRE(update_chart_plot(plot,
        chart_plot_key{.version = 1, .span_ns = 3600 * second}, ring.columns()));
    CHECK(hedgr::testing::allocations() - before == 0);
    CHECK(plot.low == 1.0);
    CHECK(plot.high == 3600.0);
    CHECK(plot.ema.empty());
    CHECK(plot.candles.empty());
}

TEST_CASE("an empty ring leaves a plot with nothing in view", "[chart]") {
    chart_plot plot;
    const auto ring = seconds(1, 10);
    update_chart_plot(plot, chart_plot_key{.version = 1, .candles = true,
        .ema = true}, ring.columns());
    REQUIRE_FALSE(plot.candles.empty());
    REQUIRE(update_chart_plot(plot, chart_plot_key{.version = 0, .candles = true,
        .ema = true}, series_columns{}));
    CHECK(plot.candles.empty());
    CHECK(plot.ema.empty());
    CHECK(chart_plot_start(plot, series_columns{}) == 0);
}

TEST_CASE("the cost of a chart frame over one hour", "[!benchmark][chart]") {
    const auto ring = seconds(1, 3600);
    const auto points = ring.columns();
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
        return fresh.candles.size();
    };
    chart_plot kept;
    update_chart_plot(kept, key, points);
    BENCHMARK("frame with nothing new") {
        return update_chart_plot(kept, key, points);
    };
}
