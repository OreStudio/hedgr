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
namespace state = hedgr::model::state;

/** @brief The first index of the points that fall in @p span_ns. */
[[nodiscard]] std::size_t live_window_start(const state::series_columns& points,
    std::int64_t span_ns) {
    if (points.size < 2 || span_ns <= 0)
        return 0;
    const auto cutoff = points.at_ns[points.size - 1] - span_ns;
    const auto* const found = std::lower_bound(points.at_ns,
        points.at_ns + points.size - 1, cutoff);
    const auto first = static_cast<std::size_t>(found - points.at_ns);
    /* Two points are the least a line needs; a sparse series keeps it all. */
    return points.size - first < 2 ? 0 : first;
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
void build_candles(const state::series_columns& points, std::size_t start,
    std::int64_t width_ns, std::vector<candle>& candles) {
    candles.clear();
    if (width_ns <= 0)
        return;
    for (std::size_t i = start; i < points.size; ++i) {
        const double value = points.value[i];
        const double open = points.open[i] != 0.0 || points.high[i] != 0.0
                || points.low[i] != 0.0
            ? points.open[i]
            : value;
        const double high = points.high[i] != 0.0 ? points.high[i] : value;
        const double low = points.low[i] != 0.0 ? points.low[i] : value;
        const auto bucket = points.at_ns[i] / width_ns;
        if (candles.empty() || candles.back().at_ns / width_ns != bucket) {
            candles.push_back(candle{.at_ns = bucket * width_ns,
                .open = open, .high = high, .low = low, .close = value});
            continue;
        }
        auto& last = candles.back();
        last.high = std::max(last.high, high);
        last.low = std::min(last.low, low);
        last.close = value;
    }
}

}

bool update_chart_plot(chart_plot& plot, const chart_plot_key& key,
    const state::series_columns& points) {
    if (plot.key == key)
        return false;
    plot.key = key;
    plot.xs.clear();
    plot.ys.clear();
    plot.ema.clear();
    plot.candles.clear();
    plot.start = live_window_start(points, key.span_ns);
    if (points.size == 0)
        return true;

    plot.low = points.value[points.size - 1];
    plot.high = plot.low;
    plot.xs.assign(points.at_s + plot.start, points.at_s + points.size);
    plot.ys.assign(points.value + plot.start, points.value + points.size);
    for (const auto value : plot.ys) {
        plot.low = std::min(plot.low, value);
        plot.high = std::max(plot.high, value);
    }

    /*
     * The selected span sizes the candles. All means the whole series, so the
     * width comes from the points actually in view.
     */
    const auto candle_span = key.span_ns == 0
        ? points.at_ns[points.size - 1] - points.at_ns[plot.start]
        : key.span_ns;
    plot.candle_width_ns = candle_width_for(candle_span);
    if (key.candles)
        build_candles(points, plot.start, plot.candle_width_ns, plot.candles);
    if (key.ema && plot.ys.size() > 1)
        exponential_moving_average(plot.ys, plot.ema);
    return true;
}

}
