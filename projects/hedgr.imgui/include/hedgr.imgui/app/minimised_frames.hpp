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
#ifndef HEDGR_IMGUI_APP_MINIMISED_FRAMES_HPP
#define HEDGR_IMGUI_APP_MINIMISED_FRAMES_HPP

namespace hedgr::imgui::app {

/**
 * @brief Whether a frame is skipped while the main window is minimised.
 *
 * A minimised main window shows nothing, so with no other window the loop
 * idles. Windows torn out into their own OS windows are still on screen, so
 * the loop keeps drawing them, at the slower pace below because some X11
 * drivers stop pacing the main window's swap once it is iconified.
 *
 * @param minimised The main window is iconified or has no framebuffer.
 * @param capturing A capture counts frames and is never paused.
 * @param viewport_count ImGui's platform viewports, the main one included.
 */
[[nodiscard]] constexpr bool skips_frame_while_minimised(bool minimised,
    bool capturing, int viewport_count) noexcept {
    return minimised && !capturing && viewport_count <= 1;
}

/** @brief The pause after a frame drawn for torn-out windows alone, 30 Hz. */
inline constexpr double torn_out_frame_wait_s = 1.0 / 30.0;

}

#endif
