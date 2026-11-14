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
#ifndef HEDGR_UI_RENDER_SURFACE_PLOT_HPP
#define HEDGR_UI_RENDER_SURFACE_PLOT_HPP

#include "hedgr.model/state/surface_table.hpp"
#include "hedgr.ui/export.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace hedgr::ui::render {

/**
 * @brief What a surface plot answers, so it is rebuilt only when it moved.
 *
 * The surface's stamp is the table's own, and the instant travels with it
 * because a republish at the same stamp is still a new surface. A view that
 * changed neither is the view already drawn.
 */
struct surface_plot_key final {
    std::uint64_t version = 0;
    std::int64_t as_of_ns = 0;

    [[nodiscard]] bool operator==(const surface_plot_key&) const = default;
};

/**
 * @brief A surface as the 3D plot library wants it, kept between frames.
 *
 * The plot library takes three flat arrays - one coordinate per vertex, each
 * grid of @c x_count by @c y_count vertices - so the grid is flattened once,
 * when the surface changes, and not on every frame. The axes are categorical
 * (a strike is "25D", an expiry is "3M"), so the coordinates are positions on
 * the grid and the labels ride beside them as the axis ticks.
 *
 * The values stay row-major with the expiry varying slowest, which is the
 * orientation the deployment published them in: a reader who has seen the
 * table and a reader who has seen the plot are looking at the same thing.
 */
struct surface_plot final {
    /** @brief What the plot answers, valid once @c builds is above zero. */
    surface_plot_key key;

    /** @brief The vertices' strike coordinate, one per grid point. */
    std::vector<double> xs;

    /** @brief The vertices' expiry coordinate, one per grid point. */
    std::vector<double> ys;

    /** @brief The volatility at each grid point, row-major. */
    std::vector<double> zs;

    /** @brief Where each strike label sits on the x axis. */
    std::vector<double> strike_at;
    std::vector<std::string> strike_text;
    std::vector<const char*> strike_labels;

    /** @brief Where each expiry label sits on the y axis. */
    std::vector<double> expiry_at;
    std::vector<std::string> expiry_text;
    std::vector<const char*> expiry_labels;

    /** @brief The grid's shape: strikes across, expiries deep. */
    std::size_t x_count = 0;
    std::size_t y_count = 0;

    /** @brief The range of the grid, for the plot's colour scale. */
    double low = 0.0;
    double high = 0.0;

    /** @brief How many times it has been rebuilt. */
    std::uint64_t builds = 0;
};

/**
 * @brief Rebuilds @p plot when @p key differs from the one it answers.
 *
 * An incomplete grid is not plotted: a surface with a hole in it is a read that
 * has not finished, and drawing the hole as a zero volatility would put a cliff
 * in a curve the deployment never stated.
 *
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_surface_plot(surface_plot& plot,
    const surface_plot_key& key,
    const hedgr::model::state::surface_row& row,
    const hedgr::model::state::surface_table& surfaces);

}

#endif
