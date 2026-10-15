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
#ifndef HEDGR_UI_RENDER_PANELS_HPP
#define HEDGR_UI_RENDER_PANELS_HPP

#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/export.hpp"
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @brief The visible name of a window: what it shows, not what it is.
 *
 * A title is read from the window's kind-specific state, which is cold, and
 * only for a window that survived the frame loop's culling gates. The layers
 * whose names the state holds -- an instance's name, an account's -- are
 * resolved through the tables here, because a window's persisted state carries
 * keys and a person reads names.
 *
 * The kinds that name something are named; one that does not falls back to its
 * own kind, so a restored window whose subject has since been removed still has
 * a title rather than an empty one.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::string window_title(
    hedgr::model::domain::window_kind kind,
    const hedgr::model::domain::window_state& state,
    const render_input& input);

/**
 * @brief Whether the store's contents can be read.
 *
 * This is the lock gate, and it is a function rather than a condition inside
 * the drawing loop so that it can be tested without a GL context. It answers
 * one question: is there anything in the store that may be shown?
 *
 * Every read of the store is a decryption. Instances and folders were never
 * secrets on their own, but they live in a file that is unreadable without the
 * master password, so while it is not held there are rows that must not be
 * drawn. The alternative -- showing a catalogue that cannot be written to --
 * would let an operator edit rows that would then fail to save.
 *
 * It is deliberately not "is the Instances window drawn". The window is the
 * shell: the lock control lives on its status bar, so it stays on screen in
 * every state and its renderer asks this function whether to blank its rows.
 * @c absent and @c locked both close the gate; @c absent is the first run and
 * @c failed closed it again after a refusal. Nothing here reports an error,
 * because a closed lock is a state and not a fault.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
shows_instances(const render_input& input) noexcept;

/**
 * @brief Whether a window of @p kind is drawn while the store is locked.
 *
 * Asked about one window rather than about the whole interface, so the frame
 * loop can cull a catalogue window without deciding what one is. The password
 * form is not a window of a kind and is not gated.
 *
 * The accounts windows and the workspaces stay hidden until the store opens,
 * because a surface that held a stale copy of the catalogue would be worse
 * than no surface: it would look like data and be a memory.
 *
 * The Instances window is the exception, because it is the shell: the lock
 * control is on its status bar and an operator whose whole program vanished
 * when the store locked would be looking at a different program from the one
 * they are about to use. It is drawn, and its renderer blanks its own rows
 * from @c shows_instances.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
shows_window(hedgr::model::domain::window_kind kind, const render_input& input,
    std::string_view window_id) noexcept;

/**
 * @brief What the startup notice says, as a heading and the reason under it.
 *
 * The reason is the runtime's own sentence, carried through unchanged: it
 * already names the file, both schema versions where that is the fault, and
 * the one thing an operator can do about it. Rewriting it here would be a
 * second copy of the store's diagnosis, free to drift from the first.
 */
struct startup_notice final {
    /** @brief What the notice is about, in a few words. */
    std::string heading;

    /** @brief The failure as the runtime recorded it. */
    std::string reason;

    /** @brief Whether there is a notice to draw at all. */
    [[nodiscard]] bool empty() const noexcept { return reason.empty(); }
};

/**
 * @brief The notice for @p startup_failure, which is empty when there is none.
 *
 * A pure function of the failure text so that the decision to warn the operator
 * can be tested without a GL context, which is what every other rule in this
 * component does. Nothing here reads the store: the whole point of the notice
 * is that it is the one surface left when the store is what failed.
 */
[[nodiscard]] HEDGR_UI_EXPORT startup_notice
startup_notice_for(std::string_view startup_failure);

}

#endif
