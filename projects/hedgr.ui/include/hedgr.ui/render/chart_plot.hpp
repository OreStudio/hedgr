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
#ifndef HEDGR_UI_RENDER_CHART_PLOT_HPP
#define HEDGR_UI_RENDER_CHART_PLOT_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace hedgr::ui::render {

/** @brief One candle, built from the points of its interval. */
struct candle final {
    std::int64_t at_ns = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

/** @brief Everything a chart's plot data is derived from, besides the points. */
struct chart_plot_key final {
    /** @brief The snapshot's version, or 0 when there is no snapshot. */
    std::uint64_t version = 0;
    std::int64_t span_ns = 0;
    bool candles = false;
    bool ema = false;

    [[nodiscard]] bool operator==(const chart_plot_key&) const = default;
};

/**
 * @brief A chart's plot arrays, kept between frames.
 *
 * The points change about once a second and a frame comes sixty times a
 * second, so the arrays are rebuilt only when the key changes, and a rebuild
 * reuses the vectors' storage.
 */
struct chart_plot final {
    chart_plot_key key;

    /** @brief The first index of the points in view. */
    std::size_t start = 0;

    /** @brief Seconds since the epoch and values of the points in view. */
    std::vector<double> xs;
    std::vector<double> ys;

    /** @brief The moving average of @c ys, or empty when it is off. */
    std::vector<double> ema;

    /** @brief The candles, or empty when the chart draws a line. */
    std::vector<candle> candles;
    std::int64_t candle_width_ns = 0;

    /** @brief The range of @c ys. */
    double low = 0.0;
    double high = 0.0;
};

/**
 * @brief Rebuilds @p plot from @p points when @p key differs from its own.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_chart_plot(chart_plot& plot,
    const chart_plot_key& key,
    const std::deque<hedgr::model::domain::market_point>& points);

}

#endif
