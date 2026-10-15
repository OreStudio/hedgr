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
#ifndef HEDGR_IMGUI_APP_CAPTURE_INPUT_HPP
#define HEDGR_IMGUI_APP_CAPTURE_INPUT_HPP

struct GLFWwindow;

namespace hedgr::imgui::app {

/**
 * @brief Makes a capture's own input the only mouse ImGui hears.
 *
 * The desktop's pointer competes with the one a capture drives: the GLFW
 * backend reports the cursor every frame and ImGui takes the last event, so a
 * synthetic click is overwritten by wherever the machine's mouse happens to be.
 * A capture whose input is not the only input measures the desk rather than the
 * interface.
 *
 * Called after the window exists and before the first frame. A run with no
 * capture script is left alone and keeps every ordinary GLFW callback.
 *
 * @param window The window whose input is to be taken over.
 * @return Whether a capture script was found and the callbacks were replaced.
 */
[[nodiscard]] bool install_capture_input(GLFWwindow* window);

}

#endif
