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
 * @brief Whether the Instances window is drawn at all.
 *
 * This is the lock gate, and it is a function rather than a condition inside
 * the drawing loop so that it can be tested without a GL context. The rule is
 * one line: the catalogue is drawn only while the store is unlocked.
 *
 * The reason it is the whole catalogue and not just the accounts is that every
 * read of the store is a decryption. Instances and folders were never secrets
 * on their own, but they live in a file that is now unreadable without the
 * master password, so while it is not held there is genuinely nothing to draw.
 * The alternative -- showing a catalogue that cannot be written to -- would let
 * an operator edit rows that would then fail to save, which is worse than
 * asking for the password first.
 *
 * @c absent and @c locked both close the gate. @c absent is the first run,
 * where the operator has to create a password before there is a store worth
 * opening; @c failed closed it again after a refusal. Nothing here reports an
 * error, because a closed lock is a state and not a fault.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
shows_instances(const render_input& input) noexcept;

/**
 * @brief Whether a window of @p kind is drawn while the store is locked.
 *
 * The same gate as @c shows_instances, asked about one window rather than about
 * the whole interface, so the frame loop can cull a catalogue window without
 * deciding what a catalogue window is. The prompt that asks for the password is
 * not a window and is not gated.
 *
 * A window kind that shows something read from the store stays hidden until the
 * store opens, because a surface that holds a stale copy of the catalogue would
 * be worse than an empty screen: it would look like data and be a memory.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
shows_window(hedgr::model::domain::window_kind kind, const render_input& input,
    std::string_view window_id) noexcept;

}

#endif
