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
#ifndef HEDGR_UI_WIDGET_CELLS_HPP
#define HEDGR_UI_WIDGET_CELLS_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <string_view>

namespace hedgr::ui::widget {

/**
 * @brief The space between two letters of a caption, at the current font.
 *
 * ImGui has no letter spacing, so a caption draws one glyph at a time and adds
 * this after every glyph but the last.
 */
[[nodiscard]] HEDGR_UI_EXPORT float caption_tracking();

/**
 * @brief The size @p text takes as a caption, help glyph included.
 *
 * Measured the way @c caption draws: in capitals, in the small bold face, with
 * the tracking between letters. A row laid out from this size is the row that
 * is drawn.
 *
 * @param help The help text; when it is not empty a circled question mark
 * follows the caption and is counted in the width.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 caption_size(std::string_view text,
    std::string_view help = {});

/**
 * @brief Draws a small, dim, letter-spaced capital label and advances past it.
 *
 * Records one item of @c caption_size, so SameLine and the width queries see
 * the tracking. On a line taller than itself, as after AlignTextToFramePadding,
 * it sits in the middle of the line as text does. Lower case ASCII is drawn in
 * capitals; the caller's text is not copied.
 *
 * @param help Shown on hover over the caption, empty for no help glyph. The
 * glyph is a dim ring round a question mark, and both brighten while the
 * caption is hovered, so the reader sees which label the tooltip explains.
 */
HEDGR_UI_EXPORT void caption(std::string_view text, std::string_view help = {});

/**
 * @brief Draws a large bold value, such as a price, and advances past it.
 *
 * About a third larger than body text and in the bold face, or the regular
 * face where no bold file is installed. Drawn in the ordinary text colour and
 * never a state colour: a pill beside it carries the direction. Every digit
 * takes the widest digit's advance, so a ticking value keeps its width. Centred
 * on a line taller than itself, as a caption is.
 */
HEDGR_UI_EXPORT void headline_value(std::string_view text);

/** @brief The corner radius of a value pill: a chip rather than a capsule. */
inline constexpr float value_pill_rounding = 4.0f;

/** @brief The width of a value pill's outline. */
inline constexpr float value_pill_border = 1.0f;

/** @brief The size a value pill showing @p text takes. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 value_pill_size(std::string_view text);

/**
 * @brief Draws a bordered chip with bold text and advances past it.
 *
 * Centred on a line taller than itself, as a caption is. Neutral is the chip
 * fill for a value with no verdict. Good, bad and warning take the theme's
 * state colours, never the accent.
 */
HEDGR_UI_EXPORT void value_pill(std::string_view text,
    theme::tone tone = theme::tone::neutral);

/**
 * @brief The height of one labelled value cell, plain or pill.
 *
 * One height for both, so a row that mixes plain values and pills lines up.
 */
[[nodiscard]] HEDGR_UI_EXPORT float labelled_value_height();

/**
 * @brief Draws a caption over a bold value, in a cell of @p width.
 *
 * Records one item of @p width by @c labelled_value_height, so cells placed
 * with SameLine(0, 0) form columns. Text wider than the cell is clipped to it.
 */
HEDGR_UI_EXPORT void labelled_value(std::string_view label,
    std::string_view value, float width, std::string_view help = {});

/** @brief Draws a caption over a value pill, in a cell of @p width. */
HEDGR_UI_EXPORT void labelled_pill(std::string_view label,
    std::string_view value, theme::tone tone, float width,
    std::string_view help = {});

/** @brief The height of one two-line value cell. */
[[nodiscard]] HEDGR_UI_EXPORT float labelled_value2_height();

/**
 * @brief Draws a caption over a bold value over a dim line, in a cell of
 * @p width.
 *
 * The reference's DATE cell: the date in bold and its time beneath it, dim
 * and in the regular face, because the time refines the date rather than
 * standing beside it. Records one item of @p width by
 * @c labelled_value2_height, which is taller than a one-line cell.
 */
HEDGR_UI_EXPORT void labelled_value2(std::string_view label,
    std::string_view value, std::string_view sub, float width,
    std::string_view help = {});

/** @brief The height of a hero strip. */
[[nodiscard]] HEDGR_UI_EXPORT float hero_strip_height();

/**
 * @brief Draws a full width tinted band: a caption at the left, a large value
 * and an optional pill at the right.
 *
 * Takes the whole available width and records one item of that width by
 * @c hero_strip_height.
 *
 * @param pill The pill's text, empty for none. The pill takes @p tone too.
 */
HEDGR_UI_EXPORT void hero_strip(std::string_view label, std::string_view value,
    theme::tone tone, std::string_view pill = {}, std::string_view help = {});

}

#endif
