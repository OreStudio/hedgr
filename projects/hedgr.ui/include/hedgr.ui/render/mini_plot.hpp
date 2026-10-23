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
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hedgr::ui::render {

/** @brief One cell of the card grid, counted from the grid's origin. */
struct card_cell final {
    int column = 0;
    int row = 0;

    [[nodiscard]] friend bool operator==(card_cell, card_cell) noexcept = default;
};

/**
 * @brief The grid cards snap to.
 *
 * Cells start at @c origin and repeat every @c pitch, which is a card plus its
 * gap. @c columns and @c rows are the cells that fit the desk, at least one
 * each, and bound where a card is moved to.
 */
struct card_grid final {
    ImVec2 origin;
    ImVec2 pitch;
    int columns = 1;
    int rows = 1;
};

/** @brief The cell whose corner is nearest @p position, inside the grid. */
[[nodiscard]] HEDGR_UI_EXPORT card_cell cell_for(const card_grid& grid,
    ImVec2 position) noexcept;

/** @brief The top-left corner of @p cell. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 cell_position(const card_grid& grid,
    card_cell cell) noexcept;

/**
 * @brief The cell a card dropped at @p position takes.
 *
 * Its own cell when that is free, else the free cell whose corner is nearest
 * @p position, the first in row order on a tie. When every cell is taken the
 * card keeps its own.
 */
[[nodiscard]] HEDGR_UI_EXPORT card_cell snap_cell(const card_grid& grid,
    ImVec2 position, std::span<const card_cell> occupied) noexcept;

/** @brief The corner a card dropped at @p position snaps to. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 snap_position(const card_grid& grid,
    ImVec2 position, std::span<const card_cell> occupied) noexcept;

/**
 * @brief The first free cell, left to right then top to bottom.
 *
 * Rows past the grid's own continue the scan, so a full desk still gets a cell.
 */
[[nodiscard]] HEDGR_UI_EXPORT card_cell first_free_cell(const card_grid& grid,
    std::span<const card_cell> occupied) noexcept;

/**
 * @brief The span a card shows.
 *
 * It follows the open chart of the same series and session, which is how a
 * reader sees the card and the chart agree. With no such chart it is the span
 * the card was opened with.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t mini_plot_span(
    const render_input& input, hedgr::model::state::window_handle card);

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
