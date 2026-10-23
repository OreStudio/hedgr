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
#ifndef HEDGR_UI_WIDGET_SEGMENTED_HPP
#define HEDGR_UI_WIDGET_SEGMENTED_HPP

#include "hedgr.ui/export.hpp"
#include "imgui.h"

namespace hedgr::ui::widget {

/**
 * @brief A choice drawn as a rounded group of segments, one for each option.
 *
 * The options are borrowed, as a foot combo's are, and are drawn in order.
 */
struct segmented_control final {
    /** @brief ImGui id, unique among the controls of one window. */
    const char* id;
    /** @brief What the control chooses, shown on hover. */
    const char* tooltip;
    const char* const* options;
    int option_count;
    /** @brief The chosen option, clamped to the options on every draw. */
    int* selected;
};

/** @brief The width the group takes. */
[[nodiscard]] HEDGR_UI_EXPORT float segmented_width(
    const segmented_control& control);

/** @brief The height the group takes. */
[[nodiscard]] HEDGR_UI_EXPORT float segmented_height();

/**
 * @brief Draws the group with its top left at @p origin, in screen space.
 *
 * Moves the cursor, so the caller puts it back. Allocates nothing.
 *
 * @return whether the choice changed this frame.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool draw_segmented(
    const segmented_control& control, ImVec2 origin);

}

#endif
