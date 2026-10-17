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

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/export.hpp"
#include <cstdint>
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
/**
 * @brief Whether pressing Sign in has to ask for a password first.
 *
 * Two reasons and only two. A closed store cannot be read, so a remembered
 * password is out of reach. An account with no remembered password has nothing
 * to read even when the store is open. The rule lives here so it can be tested
 * without a window, and so the button and the prompt cannot disagree about it.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool sign_in_needs_password(
    hedgr::model::domain::store_lock lock, bool has_credential) noexcept;

[[nodiscard]] HEDGR_UI_EXPORT bool
shows_window(hedgr::model::domain::window_kind kind, const render_input& input,
    std::string_view window_id) noexcept;

/**
 * @brief Asks for the accounts window of one instance.
 *
 * The accounts window is found or made by the runtime, which owns the window
 * table: the interface names the instance it wants and the runtime answers with
 * the one window showing it. A second request focuses the window that exists
 * rather than making another.
 *
 * The action lives here rather than at either of its callers because the
 * Instances window's own toolbar and the shortcuts bar both open an instance's
 * accounts, and one description of what that means is what stops the two from
 * drifting.
 *
 * An instance the table does not hold is refused here rather than sent. The
 * runtime would have nothing to bind the window to, and a window bound to
 * nothing is a surface that can only report its own emptiness.
 */
HEDGR_UI_EXPORT void request_accounts_window(const render_input& input,
    std::string_view instance_id);

/**
 * @brief Asks for the Instances window.
 *
 * The window is found or made by the runtime, which owns the window table. The
 * operator can close the root like any other window, and this is the request
 * that brings it back. A request for a window the table already holds sets it
 * open rather than adding a second, so the same call both raises a window on
 * screen and reopens one that was closed.
 *
 * The shortcut bar asks through this because its Instances entry is a shortcut
 * like any other and the bar should not have to know how a window is made.
 */
HEDGR_UI_EXPORT void request_instances_window(const render_input& input);

/**
 * @brief The decoded picture of one instance, as a backend texture, or zero.
 *
 * Zero means the instance has no picture rather than that the drawing failed.
 * An instance typed by hand carries no illustration key, and a key whose asset
 * is not installed has no texture either; both are ordinary states and the
 * caller draws its own fallback for them. The keys are resolved here because
 * both the shortcuts bar and the details pane ask the same question of the same
 * row.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::uint64_t instance_icon_texture(
    const render_input& input, std::string_view instance_id);

/**
 * @brief The stable key a bar entry carries for one target.
 *
 * Derived from the kind and the target rather than counted, so the entry an
 * operator adds and the entry they later remove name the same row without
 * either side having to remember what the other wrote. The kind is in the key
 * because an instance and an account can carry the same id, and the two
 * shortcuts are different entries.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::string shortcut_id_for(
    hedgr::model::domain::window_kind kind, std::string_view target_id);

/**
 * @brief Whether the bar already holds an entry that opens this target.
 *
 * The match is on the kind and the id rather than on the derived key, for the
 * same reason the store's own removal matches on both halves: a shortcut is a
 * statement about a target, and two entries claiming one target are one fact
 * said twice.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool shortcuts_bar_holds(
    const hedgr::model::state::shortcut_table& table,
    hedgr::model::domain::window_kind kind, std::string_view target_id);

/**
 * @brief The position a new entry takes, which is after every entry already on
 * the bar.
 *
 * A new entry lands at the end rather than at the front, because a bar that
 * rearranged itself as entries were added would be an operator's own order
 * being overwritten by their next click.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t next_shortcut_position(
    const hedgr::model::state::shortcut_table& table) noexcept;

/**
 * @brief The command that puts a target on the bar, or takes it off.
 *
 * One function rather than two branches at the control, because the state the
 * control draws and the command it sends have to be the same fact. A control
 * showing that a target is present and then sending the save would be a click
 * that appears to do nothing, and the two would drift the moment one of them
 * changed.
 *
 * The removal names the derived key, which is what the store removes by, and
 * the save carries the same key so that an entry added now is the entry removed
 * later.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::model::command::command
shortcut_toggle_command(const render_input& input,
    hedgr::model::domain::window_kind kind, std::string_view target_id);

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
