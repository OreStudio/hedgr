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
#ifndef HEDGR_UI_RENDER_WORKSPACE_HPP
#define HEDGR_UI_RENDER_WORKSPACE_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include <string>
#include <unordered_map>

namespace hedgr::ui::render {

/**
 * @brief The frame: every surface Hedgr draws, in one call.
 *
 * The workspace owns the interface's scratch state and nothing else. All
 * durable state lives in the model's tables, and all change reaches them
 * through the command queue, so the workspace can be destroyed and rebuilt
 * without losing anything a person would miss.
 *
 * There is no menu bar. Actions are icon buttons with tooltips, because a
 * pull-down menu hides what can be done behind a click that does not name it.
 */
class HEDGR_UI_EXPORT workspace final {
public:
    /** @brief Starts with no scratch state, which is exactly a fresh frame. */
    workspace();

    /** @brief Out of line so callers need no ImGui headers to hold one. */
    ~workspace();

    workspace(const workspace&) = delete;
    workspace& operator=(const workspace&) = delete;

    /**
     * @brief Draws the toolbar, then the Instance Manager, then every open
     * window.
     *
     * Must be called between ImGui::NewFrame and ImGui::Render. Pushes at most
     * one command per user action into input.commands; it never touches the
     * tables it reads.
     */
    void render(const render_input& input);

private:
    /**
     * @brief Scratch state per window, keyed by the window's stable id.
     *
     * Keyed by stable id rather than by handle so that a window which is
     * closed and reopened, or which survives a rebuild of the window table,
     * keeps whatever the person had typed into it.
     */
    std::unordered_map<std::string, window_ui_state> ui_state_;
};

}

#endif
