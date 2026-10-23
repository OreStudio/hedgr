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
#ifndef HEDGR_UI_WIDGET_TOOLBAR_HPP
#define HEDGR_UI_WIDGET_TOOLBAR_HPP

#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include <cstddef>

namespace hedgr::ui::widget {

/** @brief What a button of the floating toolbar does, in the order drawn. */
enum class toolbar_action {
    series_browser,
    accounts,
    undock_all,
};

/** @brief How many buttons the toolbar has. */
inline constexpr std::size_t toolbar_action_count = 3;

/** @brief The facts that decide which buttons can be used. */
struct toolbar_availability final {
    /** @brief A session is signed in, which the windows it opens belong to. */
    bool session = false;
    /** @brief Something is docked in the desk. */
    bool docked = false;
};

/** @brief A rectangle by its corners. */
struct toolbar_rect final {
    ImVec2 min;
    ImVec2 max;
};

/** @brief Whether the button for @p action can be pressed. */
[[nodiscard]] HEDGR_UI_EXPORT bool toolbar_enabled(toolbar_action action,
    toolbar_availability available) noexcept;

/** @brief The capsule's size: the grip, then @c toolbar_action_count buttons. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 toolbar_size() noexcept;

/** @brief The grip's hit area, for a capsule whose top left is @p origin. */
[[nodiscard]] HEDGR_UI_EXPORT toolbar_rect toolbar_grip_rect(
    ImVec2 origin) noexcept;

/** @brief Button @p index's rectangle, for a capsule at @p origin. */
[[nodiscard]] HEDGR_UI_EXPORT toolbar_rect toolbar_button_rect(
    ImVec2 origin, std::size_t index) noexcept;

/**
 * @brief The top left that keeps a capsule of @p size inside the work area.
 *
 * The work area is @p work_pos and @p work_size, which already stop short of
 * the menu bar. The capsule keeps @c theme::toolbar_edge_margin clear of the
 * left, right and bottom edges and may touch the top. An area too small for the
 * capsule pins it to the top left.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 toolbar_clamp(ImVec2 pos, ImVec2 size,
    ImVec2 work_pos, ImVec2 work_size) noexcept;

/** @brief Where a capsule first appears: centred, just under the menu bar. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec2 toolbar_default_pos(ImVec2 size,
    ImVec2 work_pos, ImVec2 work_size) noexcept;

/**
 * @brief A flat square icon button: the glyph centred, a fill only under the
 * pointer, and @p tooltip on hover.
 *
 * The one look of a header icon and a toolbar button. A disabled button is
 * dimmed, takes no hover fill and returns false. Returns whether it was
 * pressed.
 */
HEDGR_UI_EXPORT bool icon_tile(const char* id, const char* glyph,
    const char* tooltip, ImVec2 origin, float side, bool enabled = true);

}

#endif
