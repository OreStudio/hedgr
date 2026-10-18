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
 *
 * It carries a close control like every other window. The shortcut bar's entry
 * for it is what opens it again, and that entry is only drawn while the store
 * is open, so the close control is offered only then as well.
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
 * @brief Selects an instance in the Instances window, on the next frame it
 *        draws.
 *
 * The read-only pane is reached by selecting a row, and a capture that wants to
 * show it without a click has no other way in. The selection is the same
 * scratch a click writes, so the pane that follows is the one an operator would
 * have seen.
 */
HEDGR_UI_EXPORT void select_instance_for_capture(std::string_view instance_id);

/**
 * @brief Selects an account in the Accounts window, on the next frame it draws.
 *
 * The account pane is reached by selecting a row, and a capture that wants to
 * show it without a click has no other way in. The selection is the same
 * scratch a click writes, so the pane that follows is the one an operator would
 * have seen.
 */
HEDGR_UI_EXPORT void select_account_for_capture(std::string_view account_id);

/**
 * @brief Puts the Instances pane into edit mode on one row, for a capture.
 *
 * Edit mode is otherwise reached by clicking the row's edit icon, and a capture
 * is not a person. What this writes is that click's own scratch -- the selected
 * row and the mode -- so the drafts are then filled from the row by the same
 * code the click reaches.
 */
HEDGR_UI_EXPORT void open_instance_edit_for_capture(std::string_view instance_id);

/**
 * @brief Overwrites one in-place edit draft, for a capture.
 *
 * A capture has no keyboard, so it cannot type a change into the pane and the
 * frame that has to show a changed field could not be reached by a person's own
 * gesture. This writes the draft the way typing would, and does nothing unless
 * an edit is already being filled, so it can never invent a change out of an
 * ordinary run.
 *
 * @param field The field's own name as the pane spells it, which is one of
 *        name, host, port, subject, wire_format, tls_ca, tls_cert, tls_key or
 *        labels.
 * @param value The value to write into that draft.
 */
HEDGR_UI_EXPORT void change_instance_field_for_capture(std::string_view field,
    std::string_view value);

/**
 * @brief Closes the Instances window on the frame it next draws, for a capture.
 *
 * The close control is a title bar button that no scripted step can name, and
 * the frame that has to show the closed root cannot be reached through any
 * observed control. This pushes the command that the control pushes and nothing
 * else, so the closed frame is the one a click would have produced.
 */
HEDGR_UI_EXPORT void close_instances_for_capture();

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

/**
 * @brief The deployment's own screens, for a super administrator.
 *
 * A menu bar for the areas and a tab strip for the screens that are open. The
 * Overview is pinned and always first; every other screen is opened from a menu
 * and closed from its own tab. The window is one per session, so a wall may
 * hold several, each showing a different deployment.
 */
/**
 * @brief The market series a session may plot, as a chooser.
 *
 * One list, filtered by what the person types, and a double click on a row
 * opens that series' chart. The window stays open, so several plots are opened
 * from the same list without going back to the menu each time.
 */
HEDGR_UI_EXPORT void render_series_browser(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

/** @brief One interest-rate curve, tenor against rate. */
HEDGR_UI_EXPORT void render_term_structure(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

HEDGR_UI_EXPORT void render_command_centre_window(
    hedgr::model::state::window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock);

/**
 * @brief Opens a command centre screen, for a capture.
 *
 * A capture has no pointer, so it cannot work a menu. This writes the scratch
 * the menu writes, and the next command centre drawn consumes it.
 */
HEDGR_UI_EXPORT void open_command_centre_screen_for_capture(
    std::string_view screen);

}

#endif
