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
#ifndef HEDGR_UI_RENDER_WINDOWS_HPP
#define HEDGR_UI_RENDER_WINDOWS_HPP

#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/export.hpp"

namespace hedgr::ui::render {

/**
 * @brief The Instances window: the root surface, and the only way in.
 *
 * It manages every instance and the folder tree they sit in, through the
 * browser widget that the Accounts window is built from as well. Everything it
 * can do is an icon with a tooltip, because there is no menu bar anywhere.
 */
HEDGR_UI_EXPORT void render_instances_window(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

/**
 * @brief Selects a folder in the Instances window, on the next frame it draws.
 *
 * For a capture, which has to describe a state an operator would have reached
 * by clicking. The selection set here is exactly the interface scratch a click
 * writes, so this is not a second way for the window to be driven: it is the
 * same one a mouse uses.
 */
HEDGR_UI_EXPORT void select_folder_for_capture(std::string_view folder_id);

/**
 * @brief Opens the instance form on one row, for a capture that has to show it.
 *
 * The form is otherwise reached by clicking a row's edit icon, and a capture is
 * not a person. What this writes is that click's own scratch -- the form kind
 * and the row it names -- so the fields are then filled from the row by the
 * same code the click reaches.
 */
HEDGR_UI_EXPORT void open_instance_form_for_capture(std::string_view instance_id);

/**
 * @brief The accounts of one instance, as a browser over that instance's tree.
 *
 * One window per instance, several open at once. It carries the sign-in action
 * that opens a Workspace.
 */
HEDGR_UI_EXPORT void render_accounts_window(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

/**
 * @brief The live session for one account on one instance.
 *
 * The surface that grows later, so this builds the frame and one honest panel:
 * the session's own facts, and a marked place for the panels that will be
 * driven by what the account is allowed to do.
 */
HEDGR_UI_EXPORT void render_workspace_window(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

}

#endif
