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
#ifndef HEDGR_UI_RENDER_TOOLBAR_HPP
#define HEDGR_UI_RENDER_TOOLBAR_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"

namespace hedgr::ui::render {

/** @brief What the toolbar asked for this frame. */
struct toolbar_result final {
    /** @brief Every docked window should float again. */
    bool undock_all = false;
};

/**
 * @brief Draws the floating toolbar over the desk.
 *
 * A pill-shaped window of its own, never dockable, with a grip at its left that
 * drags it and a button for each @c widget::toolbar_action. It is clamped inside
 * the main viewport's work area every frame, so it never covers the menu bar or
 * leaves the desk, and its position is the window's ImGui settings entry, which
 * is what keeps it across a restart. Nothing is drawn while the store is not
 * unlocked or while @c market_view_state::toolbar_visible is off.
 *
 * Call it after the desk's windows: it keeps itself above them, and below any
 * open popup. @p desk_docked says whether anything is docked.
 */
HEDGR_UI_EXPORT void draw_toolbar(const render_input& input, bool desk_docked,
    toolbar_result& result);

}

#endif
