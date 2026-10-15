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
#include <array>
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
     * @brief Arms the lock prompt with a password to submit on the next frame.
     *
     * For an unattended launch, where there is nobody to type one. The password
     * is held in the same buffer the prompt edits and is wiped the moment it is
     * submitted, so it exists in this object for exactly as long as a typed one
     * would. Calling this on a store that is already unlocked does nothing.
     */
    void unlock_with(std::string_view master_password);

    /**
     * @brief Draws the toolbar, then every open window, gated on the lock.
     *
     * Must be called between ImGui::NewFrame and ImGui::Render. Pushes at most
     * one command per user action into input.commands; it never touches the
     * tables it reads.
     */
    void render(const render_input& input);

private:
    /**
     * @brief Draws the toolbar, including the lock indicator and its action.
     *
     * A member and not a free function because the lock action has to change
     * this object's prompt state, and threading a reference to that through a
     * free function would hide the one thing the function is for.
     */
    void draw_toolbar(const render_input& input);

    /**
     * @brief Draws the master password prompt, when the store asks for it.
     *
     * Two modes, chosen by the state and not by the caller: @c absent asks for
     * the password twice because it is being created, and @c locked or
     * @c failed ask for it once because it is being checked.
     */
    void draw_lock_prompt(const render_input& input);

    /** @brief Whether the prompt should be on screen this frame. */
    [[nodiscard]] bool prompt_visible(const render_input& input) const noexcept;

    /** @brief Pushes a set_master_password or an unlock_store command. */
    void submit_master_password(const render_input& input);

    /** @brief Wipes both typed buffers. */
    void clear_password_buffers() noexcept;

    /**
     * @brief Scratch state per window, keyed by the window's stable id.
     *
     * Keyed by stable id rather than by handle so that a window which is
     * closed and reopened, or which survives a rebuild of the window table,
     * keeps whatever the person had typed into it.
     */
    std::unordered_map<std::string, window_ui_state> ui_state_;

    /*
     * The master password prompt's own scratch state.
     *
     * Fixed-size buffers rather than std::string because they are handed to
     * ImGui::InputText, which edits a caller-owned buffer in place. They are
     * wiped with std::fill as soon as the password is submitted, so the
     * password exists in this object only while it is being typed; the copy
     * that reaches the runtime travels in a command, which the runtime clears
     * as soon as it has used it.
     */
    std::array<char, 128> master_password_{};
    std::array<char, 128> master_password_again_{};
    bool master_password_visible_ = false;
    bool create_mismatch_ = false;

    /**
     * @brief Whether the prompt was dismissed without an answer.
     *
     * The gate stays closed, but the operator has said "not now" and is not
     * asked again this run. Without this the modal would reappear the instant
     * it was closed, which is the interface refusing to take no for an answer.
     */
    bool lock_prompt_dismissed_ = false;

    /*
     * There is deliberately no "I have submitted a password" flag here. The
     * runtime's @c unlocking already says whether a derivation is running, and
     * a local copy of that would be a second source of truth: it would survive
     * the derivation and leave the prompt stuck on "working" after a refusal,
     * with no way back to the field. The one frame between the click and the
     * runtime picking the command up looks after itself, because the buffers
     * are wiped on submit and an empty password leaves the button disabled.
     */
};

}

#endif
