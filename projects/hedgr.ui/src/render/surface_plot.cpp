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
#include "hedgr.ui/render/surface_plot.hpp"

#include <algorithm>
#include <cmath>

namespace hedgr::ui::render {

namespace state = hedgr::model::state;

bool update_surface_plot(surface_plot& plot, const surface_plot_key& key,
    const state::surface_row& row, const state::surface_table& surfaces) {
    if (plot.builds != 0 && plot.key == key)
        return false;
    plot.key = key;
    ++plot.builds;
    plot.x_count = 0;
    plot.y_count = 0;
    plot.low = 0.0;
    plot.high = 0.0;
    if (!row.complete())
        return true;

    const auto columns = row.columns;
    const auto rows = row.rows;
    plot.x_count = columns;
    plot.y_count = rows;

    const auto vertices = columns * rows;
    plot.xs.resize(vertices);
    plot.ys.resize(vertices);
    plot.zs.resize(vertices);
    for (std::size_t at = 0; at < rows; ++at) {
        for (std::size_t column = 0; column < columns; ++column) {
            const auto vertex = at * columns + column;
            plot.xs[vertex] = static_cast<double>(column);
            plot.ys[vertex] = static_cast<double>(at);
            plot.zs[vertex] = row.at(at, column);
        }
    }

    /* The colour scale spans the grid, and only the values it can draw. */
    auto low = std::numeric_limits<double>::max();
    auto high = std::numeric_limits<double>::lowest();
    for (const auto value : row.values) {
        if (!std::isfinite(value))
            continue;
        low = std::min(low, value);
        high = std::max(high, value);
    }
    if (low > high) {
        low = 0.0;
        high = 0.0;
    }
    plot.low = low;
    plot.high = high;

    /*
     * The axis ticks are the grid's own labels: a strike is a name, not a
     * number, so the coordinate is its position and the label is what the
     * blueprint called it. The text is copied because the plot library keeps
     * the pointers for as long as it draws, and a rebuild may replace the axis.
     */
    const auto& strikes = surfaces.axis(row.strike_axis);
    const auto& expiries = surfaces.axis(row.expiry_axis);
    plot.strike_at.resize(columns);
    plot.strike_text.resize(columns);
    for (std::size_t column = 0; column < columns; ++column) {
        plot.strike_at[column] = static_cast<double>(column);
        plot.strike_text[column] = column < strikes.pillars.size()
            ? strikes.pillars[column]
            : std::string{};
    }
    plot.expiry_at.resize(rows);
    plot.expiry_text.resize(rows);
    for (std::size_t at = 0; at < rows; ++at) {
        plot.expiry_at[at] = static_cast<double>(at);
        plot.expiry_text[at] = at < expiries.pillars.size()
            ? expiries.pillars[at]
            : std::string{};
    }
    plot.strike_labels.resize(columns);
    for (std::size_t column = 0; column < columns; ++column)
        plot.strike_labels[column] = plot.strike_text[column].c_str();
    plot.expiry_labels.resize(rows);
    for (std::size_t at = 0; at < rows; ++at)
        plot.expiry_labels[at] = plot.expiry_text[at].c_str();
    return true;
}

}
