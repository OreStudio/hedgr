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
#ifndef HEDGR_UI_WIDGET_TABS_HPP
#define HEDGR_UI_WIDGET_TABS_HPP

#include "hedgr.ui/export.hpp"
#include <string_view>

namespace hedgr::ui::widget {

/** @brief The thickness of the line under the chosen tab. */
inline constexpr float tab_underline_thickness = 2.0f;

/**
 * @brief Draws the accent line along the bottom of the tab just opened.
 *
 * Call it straight after a BeginTabItem that returned true, while the tab is
 * still the last item. ImGui draws its marker above a tab and only under a
 * flag; the reference marks the chosen tab from below, and this does that
 * without forking ImGui. The colour is ImGuiCol_TabSelectedOverline, which the
 * theme sets to the accent.
 */
HEDGR_UI_EXPORT void underline_selected_tab();

/**
 * @brief The width a count badge for @p count adds to a tab, the gap before it
 * included.
 */
[[nodiscard]] HEDGR_UI_EXPORT float tab_badge_count_width(int count);

/** @brief The width a label chip for @p text adds to a tab, gap included. */
[[nodiscard]] HEDGR_UI_EXPORT float tab_badge_label_width(std::string_view text);

/**
 * @brief Asks the next tab to be wide enough for @p label and a badge.
 *
 * Call it straight before the BeginTabItem that takes the same @p label. ImGui
 * sizes a tab from its label alone, so a badge drawn without this would sit on
 * top of the label or past the tab's edge.
 *
 * @param closable Whether the tab has a close button, which takes room too.
 * @param badge_width From @c tab_badge_count_width or @c tab_badge_label_width.
 */
HEDGR_UI_EXPORT void reserve_tab_badge(const char* label, bool closable,
    float badge_width);

/**
 * @brief Draws a count after the label of the tab just submitted.
 *
 * A filled capsule in the accent with the number on it, as the reference marks
 * how many signals a tab holds. Call it straight after BeginTabItem, selected
 * or not, while the tab is still the last item, having reserved its room.
 */
HEDGR_UI_EXPORT void tab_badge_count(const char* label, int count);

/**
 * @brief Draws an outlined chip after the label of the tab just submitted.
 *
 * Dim text in a dimmer outline with no fill: a tag such as PRO that names the tab
 * rather than reporting a state, so it takes neither the accent nor a state
 * colour.
 */
HEDGR_UI_EXPORT void tab_badge_label(const char* label, std::string_view text);

}

#endif
