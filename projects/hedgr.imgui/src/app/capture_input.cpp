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
#include "hedgr.imgui/app/capture_input.hpp"

#include <cstdlib>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

namespace hedgr::imgui::app {

namespace {

/*
 * A capture's pointer is the one the capture hook drives, and the desk's real
 * one must not be reported as well.
 *
 * The GLFW backend feeds ImGui the desktop cursor every frame and ImGui takes
 * the last event queued, so a synthetic position is overwritten by wherever the
 * machine's mouse happens to be. These two callbacks are registered after the
 * backend's own and replace them, which is what makes the hook's input the only
 * mouse ImGui hears.
 *
 * The position and the buttons are taken over. The enter callback is left to
 * the backend, and that is not an oversight: while it believes the pointer is
 * outside every window it falls back to reading the cursor itself, every frame,
 * and the capture's position is overwritten by the desk's within one frame. The
 * keyboard, the character stream and the window's own events are untouched
 * too, because a capture is written to ask for one frame, not to type.
 *
 * ImGui also asks for the cursor to be moved when it wants it somewhere, and
 * nothing here obeys. That costs nothing: the position ImGui acts on is its own
 * idea of where its mouse is, not the desk's.
 */

void no_cursor_pos(GLFWwindow* window, double x, double y) {
    static_cast<void>(window);
    static_cast<void>(x);
    static_cast<void>(y);
}

void no_mouse_button(GLFWwindow* window, int button, int action, int mods) {
    static_cast<void>(window);
    static_cast<void>(button);
    static_cast<void>(action);
    static_cast<void>(mods);
}

}

bool install_capture_input(GLFWwindow* window) {
    if (std::getenv("HEDGR_CAPTURE_INPUT") == nullptr
        && std::getenv("HEDGR_CAPTURE_INPUT_FILE") == nullptr)
        return false;

    /*
     * The backend's own enter handler, told that the pointer is on this window.
     * It is delivered once, as the pointer arrives, and it is what stops the
     * backend reading the desk's cursor every frame and reporting that instead.
     * The capture's own move arrives after it, so what the backend needs is
     * already there when the position starts being blocked.
     */
    ImGui_ImplGlfw_CursorEnterCallback(window, 1);
    glfwSetCursorPosCallback(window, no_cursor_pos);
    glfwSetMouseButtonCallback(window, no_mouse_button);
    return true;
}

}
