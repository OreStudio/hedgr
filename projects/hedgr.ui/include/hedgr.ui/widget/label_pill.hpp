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
#ifndef HEDGR_UI_WIDGET_LABEL_PILL_HPP
#define HEDGR_UI_WIDGET_LABEL_PILL_HPP

#include "hedgr.model/domain/label.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief The metrics one pill is measured and drawn with.
 *
 * Derived from the current font rather than fixed, so that changing the face
 * changes the shape of every pill: a hardcoded radius would stop reading as a
 * pill, and hardcoded padding would clip the text, at any other font size.
 */
struct pill_style final {
    /** @brief Horizontal padding on each side of the text. */
    float padding_x = 0.0f;

    /** @brief Vertical padding above and below the text. */
    float padding_y = 0.0f;

    /** @brief The corner radius. Half the pill's height reads as a pill. */
    float rounding = 0.0f;

    /** @brief The gap left between two pills on one line, as drawn. */
    float gap = 0.0f;

    /** @brief The text's height, which sets the pill's height with it. */
    float text_height = 0.0f;
};

/**
 * @brief The metrics a pill is drawn and laid out with, from the current style.
 *
 * Every number is derived from the font size and the style rather than being a
 * pixel constant, so a theme that installs a larger face gets larger pills
 * instead of text spilling out of a fixed box.
 */
[[nodiscard]] HEDGR_UI_EXPORT pill_style pill_style_for_current_font();

/**
 * @brief How wide a pill showing @p text is.
 *
 * The whole layout question -- whether the next pill fits, how many fit before
 * the count -- is answered from these widths, so the drawing and the counting
 * agree by construction rather than by two copies of the same arithmetic.
 */
[[nodiscard]] HEDGR_UI_EXPORT float pill_width(
    std::string_view text, const pill_style& style) noexcept;

/**
 * @brief Whether a pill of @p width fits in @p available, after a @p gap.
 *
 * The layout rule the row is built on, stated as a predicate rather than
 * inline, so it can be checked without a window: given the widths a row of
 * pills measures, this is what decides which of them are drawn and what the
 * trailing count is. A pill that does not fit whole is not drawn at all,
 * because half a pill under a neighbour is exactly the overlap a row of labels
 * must not have.
 *
 * @param gap The gap that precedes it, zero for the first pill on the line.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool pill_fits(float width, float gap,
    float available) noexcept;

/**
 * @brief How many of @p widths fit in @p available, gaps included.
 *
 * @param widths One width per pill, in the order they are drawn.
 * @param gap The gap between two neighbouring pills.
 * @return The count that fits, which is at most @c widths.size().
 */
[[nodiscard]] HEDGR_UI_EXPORT std::size_t pills_that_fit(
    const std::vector<float>& widths, float gap, float available) noexcept;

/**
 * @brief One label, ready to be drawn: the only state a pill carries.
 *
 * A value type with no ImGui and no model in it, so a host builds these from
 * whatever its rows hold and the widget's interface stays free of the model's
 * label and the theme's palette alike.
 */
struct label_pill final {
    /** @brief The label's name, as drawn inside the pill. */
    std::string text;

    /** @brief The fill, as a 32-bit ARGB value. */
    std::uint32_t background = 0;

    /** @brief The label's severity, named in the tooltip. */
    hedgr::model::domain::label_severity severity =
        hedgr::model::domain::label_severity::none;

    /**
     * @brief What makes this pill's ImGui id unique within its row.
     *
     * The label's own key, because a row may carry no two labels with one name.
     */
    std::string id;
};

/**
 * @brief Builds the pills for @p labels, filled from the theme's palette.
 *
 * The one conversion from the model's labels to what is drawn, so every surface
 * that shows a label -- a list cell, an edit form -- shows the same colours for
 * the same label.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::vector<label_pill> pills_for(
    const std::vector<const hedgr::model::domain::label*>& labels);

/**
 * @brief Draws @p pills on one line, without a frame.
 *
 * Must be called inside a window: a pill paints on that window's draw list. Each
 * pill records an item, so a pill row in a form sits in the layout like any
 * other widget and a table cell sizes its row to it.
 *
 * Pills that do not fit whole are not drawn, and a trailing @c +N pill names
 * how many were left out, listing them in its tooltip. The layout advances the
 * cursor the width of what it drew and no further, which is what keeps the next
 * widget on the line from being drawn under the last pill.
 *
 * @param pills The pills, in draw order.
 * @param available The width to lay them out in; a value of zero or less means
 * every pill fits, which is what a caller outside a table asks for.
 * @return The width the row occupied.
 */
[[nodiscard]] HEDGR_UI_EXPORT float label_pill_row(
    const std::vector<label_pill>& pills, float available);

/**
 * @brief Draws one pill and advances the cursor past it.
 *
 * The single drawing routine every surface shares, so a pill in a form and a
 * pill in a list cell are the same pixels. Records an item of the pill's size,
 * which is what makes the tooltip -- the label's name and its severity -- work.
 *
 * @param text The label's name.
 * @param background The fill, as a 32-bit ARGB value.
 * @param tooltip The text shown on hover, empty for none.
 */
HEDGR_UI_EXPORT void label_pill_draw(std::string_view text,
    std::uint32_t background, std::string_view tooltip);

}

#endif
