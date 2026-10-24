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
#ifndef HEDGR_UI_RENDER_STATUS_BAR_HPP
#define HEDGR_UI_RENDER_STATUS_BAR_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/status_bar.hpp"

namespace hedgr::ui::render {

/**
 * @brief Draws the application's bottom status strip.
 *
 * A viewport-wide plate along the bottom, submitted through ImGui's viewport
 * side bar, so the work area is inset by this and the top bar exactly once and
 * every window and the dock space read the inset without knowing about it.
 *
 * The message ladder is at the left, in the colour of its severity, dimming as
 * an event ages out. The right hand end carries the connection state of the
 * sessions: the worst news first, then how many are connected, so a bar with no
 * session says so rather than staying silent.
 *
 * It is not a window and draws no close control. The window foots stay: they
 * carry the identity of the account each window is about, which one strip
 * shared by every window could not.
 */
HEDGR_UI_EXPORT void draw_status_bar(const render_input& input,
    hedgr::ui::widget::status_bar& status);

}

#endif
