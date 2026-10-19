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
#include "hedgr.ui/render/chart_plot.hpp"
#include "hedgr.ui/render/ema.hpp"
#include <algorithm>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;

/** @brief The first index of the points that fall in @p span_ns. */
[[nodiscard]] std::size_t live_window_start(
    const std::deque<domain::market_point>& points, std::int64_t span_ns) {
    if (points.size() < 2 || span_ns <= 0)
        return 0;
    const auto cutoff = points.back().at_ns - span_ns;
    const auto found = std::lower_bound(points.begin(), points.end() - 1,
        cutoff, [](const domain::market_point& point, std::int64_t at_ns) {
            return point.at_ns < at_ns;
        });
    const auto first = static_cast<std::size_t>(found - points.begin());
    /* Two points are the least a line needs; a sparse series keeps it all. */
    return points.size() - first < 2 ? 0 : first;
}

/**
 * @brief The candle width a span reads best at.
 *
 * Around sixty candles fill the width without turning into a smear. The
 * finer rings carry one second buckets, the medium ring one minute and the
 * coarse ring one hour, so a wider candle combines the ring beneath it.
 */
[[nodiscard]] std::int64_t candle_width_for(std::int64_t span_ns) {
    if (span_ns <= 60 * domain::market_fine_ns)
        return domain::market_fine_ns;
    if (span_ns <= 300 * domain::market_fine_ns)
        return 5 * domain::market_fine_ns;
    if (span_ns <= 900 * domain::market_fine_ns)
        return 15 * domain::market_fine_ns;
    if (span_ns <= 3600 * domain::market_fine_ns)
        return domain::market_medium_ns;
    if (span_ns <= 24 * 3600 * domain::market_fine_ns)
        return 15 * domain::market_medium_ns;
    return 24 * domain::market_coarse_ns;
}

/** @brief Combines ring points into candles of @p width_ns. */
void build_candles(const std::deque<domain::market_point>& points,
    std::size_t start, std::int64_t width_ns, std::vector<candle>& candles) {
    candles.clear();
    if (width_ns <= 0)
        return;
    for (std::size_t i = start; i < points.size(); ++i) {
        const auto& point = points[i];
        const double open = point.open != 0.0 || point.high != 0.0
                || point.low != 0.0
            ? point.open
            : point.value;
        const double high = point.high != 0.0 ? point.high : point.value;
        const double low = point.low != 0.0 ? point.low : point.value;
        const auto bucket = point.at_ns / width_ns;
        if (candles.empty() || candles.back().at_ns / width_ns != bucket) {
            candles.push_back(candle{.at_ns = bucket * width_ns,
                .open = open, .high = high, .low = low, .close = point.value});
            continue;
        }
        auto& last = candles.back();
        last.high = std::max(last.high, high);
        last.low = std::min(last.low, low);
        last.close = point.value;
    }
}

}

bool update_chart_plot(chart_plot& plot, const chart_plot_key& key,
    const std::deque<domain::market_point>& points) {
    if (plot.key == key)
        return false;
    plot.key = key;
    plot.xs.clear();
    plot.ys.clear();
    plot.ema.clear();
    plot.candles.clear();
    plot.start = live_window_start(points, key.span_ns);
    if (points.empty())
        return true;

    plot.low = points.back().value;
    plot.high = points.back().value;
    plot.xs.reserve(points.size() - plot.start);
    plot.ys.reserve(points.size() - plot.start);
    for (std::size_t i = plot.start; i < points.size(); ++i) {
        const auto& point = points[i];
        plot.xs.push_back(static_cast<double>(point.at_ns) / 1.0e9);
        plot.ys.push_back(point.value);
        plot.low = std::min(plot.low, point.value);
        plot.high = std::max(plot.high, point.value);
    }

    /*
     * The selected span sizes the candles. All means the whole series, so the
     * width comes from the points actually in view.
     */
    const auto candle_span = key.span_ns == 0
        ? points.back().at_ns - points[plot.start].at_ns : key.span_ns;
    plot.candle_width_ns = candle_width_for(candle_span);
    if (key.candles)
        build_candles(points, plot.start, plot.candle_width_ns, plot.candles);
    if (key.ema && plot.ys.size() > 1)
        exponential_moving_average(plot.ys, plot.ema);
    return true;
}

}
