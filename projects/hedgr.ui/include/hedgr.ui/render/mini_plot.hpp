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
#ifndef HEDGR_UI_RENDER_MINI_PLOT_HPP
#define HEDGR_UI_RENDER_MINI_PLOT_HPP

#include "hedgr.model/state/series_ring.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace hedgr::ui::render {

/** @brief The span a card shows, fixed for now. */
inline constexpr std::int64_t mini_plot_span_ns = 24LL * 3600 * 1'000'000'000;

/** @brief The card's period label, in step with @c mini_plot_span_ns. */
inline constexpr const char* mini_plot_span_label = "24H";

/**
 * @brief The line of a card, reduced to a fixed number of points.
 *
 * Both axes are 0 to 1, x from the oldest point to the newest and y from the
 * lowest to the highest, so the shape is independent of the tile it is drawn
 * in and holds no heap storage. A series of any length costs the same.
 */
struct sparkline final {
    std::array<float, theme::mini_plot_point_budget> x{};
    std::array<float, theme::mini_plot_point_budget> y{};
    std::size_t size = 0;
};

/** @brief The first index of @p points that falls in the last @p span_ns. */
[[nodiscard]] HEDGR_UI_EXPORT std::size_t mini_plot_first(
    const hedgr::model::state::series_columns& points,
    std::int64_t span_ns) noexcept;

/**
 * @brief Reduces @p points from index @p first on to at most the point budget.
 *
 * A series with fewer points than the budget keeps all of them at their own
 * times. A longer one is sampled at even times across its span. Fewer than two
 * points make no line, so the result is empty.
 */
[[nodiscard]] HEDGR_UI_EXPORT sparkline make_sparkline(
    const hedgr::model::state::series_columns& points,
    std::size_t first) noexcept;

/**
 * @brief Draws @p line as a soft fill and one polyline inside [@p min, @p max].
 *
 * The fill is the line's colour fading to nothing at the floor, as a chart's
 * is. Nothing is drawn for an empty line.
 */
HEDGR_UI_EXPORT void draw_sparkline(ImDrawList* draw, const sparkline& line,
    ImVec2 min, ImVec2 max, ImU32 colour);

}

#endif
