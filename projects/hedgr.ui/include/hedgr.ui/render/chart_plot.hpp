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
#include "hedgr.model/state/series_ring.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
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
    /** @brief The series' stamp, or 0 when the chart has no series row. */
    std::uint64_t version = 0;
    std::int64_t span_ns = 0;
    bool candles = false;
    bool ema = false;

    [[nodiscard]] bool operator==(const chart_plot_key&) const = default;
};

/**
 * @brief What a chart derives from its points, kept between frames.
 *
 * The points change about once a second and a frame comes sixty times a
 * second, so the candles and the average are rebuilt only when the key
 * changes, and a rebuild reuses the vectors' storage. The points themselves
 * stay in the series table's columns and are never copied here.
 */
struct chart_plot final {
    chart_plot_key key;

    /**
     * @brief The sequence number of the first point in view.
     *
     * A sequence number and not an index, so it names the same point after the
     * ring compacts. @c chart_plot_start turns it into an index each frame.
     */
    std::uint64_t start_seq = 0;

    /** @brief The moving average of the points in view, or empty when it is off. */
    std::vector<double> ema;

    /** @brief The candles, or empty when the chart draws a line. */
    std::vector<candle> candles;
    std::int64_t candle_width_ns = 0;

    /** @brief The range of the values in view. */
    double low = 0.0;
    double high = 0.0;
};

/**
 * @brief Rebuilds @p plot from @p columns when @p key differs from its own.
 *
 * The columns are the ring's, valid for the frame. Nothing in the plot points
 * into them, so a compaction between frames cannot leave it dangling.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_chart_plot(chart_plot& plot,
    const chart_plot_key& key,
    const hedgr::model::state::series_columns& columns);

/**
 * @brief The index in @p columns of the first point @p plot has in view.
 *
 * Valid for the frame @p columns came from. A point that has left the ring
 * gives zero, and an empty ring gives zero.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::size_t chart_plot_start(
    const chart_plot& plot,
    const hedgr::model::state::series_columns& columns) noexcept;

}

#endif
