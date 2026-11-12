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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @brief Appends the visible name of a window: what it shows, not what it is.
 *
 * It appends rather than returns because the frame loop builds every window's
 * ImGui label in one reused buffer.
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
HEDGR_UI_EXPORT void append_window_title(std::string& out,
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
/**
 * @brief Who a window's session is and where it is, as one line.
 *
 * Every window opened from an instance has to answer three questions at a
 * glance: which deployment, which principal, and which tenant and party the
 * session acts as. The title carries the deployment; this bar carries the
 * rest, so a wall of windows over several deployments never leaves a reader
 * guessing which login they are looking at.
 */
struct identity_bar_state final {
    /** @brief The deployment, by the name a person gave it. */
    std::string instance;

    /** @brief The principal IAM accepted. Empty when nobody is signed in. */
    std::string principal;

    /** @brief The tenant IAM resolved. */
    std::string tenant;

    /** @brief The party the session is scoped to, empty for none. */
    std::string party;

    /** @brief Whether the window's session is signed in. */
    bool signed_in = false;
};

/**
 * @brief The window's session when it is signed in, or nothing.
 *
 * Signed in means authenticated and still holding a token. A window whose
 * session dropped keeps its binding, so a bound session alone is not enough.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<hedgr::model::state::session_handle>
signed_in_session(const render_input& input,
    hedgr::model::state::window_handle handle);

/**
 * @brief The session half of a window's view facts.
 *
 * What the window's binding says: no session bound, a session that has gone, or
 * one with a status, a token and a last error. The window adds what it reads.
 * The text is borrowed from the session table for the frame.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::ui::widget::view_facts
session_view_facts(const render_input& input,
    hedgr::model::state::window_handle handle);

/**
 * @brief Draws the one statement every data window makes for an unusable session.
 *
 * The mark in the body and its word on the foot, the same in every window, so
 * an operator whose session dropped is told so rather than shown a window that
 * looks as if it is still loading. Draws the foot too. The caller returns after
 * it.
 */
HEDGR_UI_EXPORT void draw_session_state(const render_input& input,
    hedgr::model::state::window_handle handle,
    hedgr::ui::widget::status_bar& status, hedgr::ui::widget::foot_state& foot);

/** @brief Resolves the bar for one window, from its session or its state. */
[[nodiscard]] HEDGR_UI_EXPORT identity_bar_state
identity_of(const render_input& input,
    hedgr::model::state::window_handle handle);

/**
 * @brief The identity as one line, "principal · tenant · party".
 *
 * All three parts are always shown. The principal names the login, the tenant
 * names the deployment's tenant, and the party names the context the session
 * acts in; a window that dropped one of them would be asking a reader to
 * remember which of the other two implied it. The dot separators keep the line
 * shorter than the older form while leaving every part readable.
 *
 * The windows put this on their status foot rather than in a bar of its own.
 * Who you are and where you are is a fact a reader checks occasionally and a
 * line they look past the rest of the time, and a strip across the top of every
 * window is a lot of chrome for occasional. At the foot, in the disabled
 * colour, it answers the question without competing with the work.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::string
identity_text(const identity_bar_state& state);

/**
 * @brief Asks the runtime for the flags a label names, once each.
 *
 * A flag is an assets image; the screen cannot block on one, so it asks and
 * draws no flag until the answer lands.
 */
HEDGR_UI_EXPORT void request_flags(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view label);

/**
 * @brief Asks the runtime for one currency's flag, once.
 *
 * Any code the tenant's image map names is asked for.
 */
HEDGR_UI_EXPORT void request_flag(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view currency);

/** @brief The uploaded texture for a currency's flag, or zero. */
[[nodiscard]] HEDGR_UI_EXPORT ImTextureID
image_texture(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view currency);

/**
 * @brief Draws a label's currency flags at the right edge of the current line.
 *
 * Both market-data screens put their flags at the right of the heading rather
 * than in front of its name: the name is what the reader came for, and a flag
 * in front of it pushes the name along by an amount that changes with the
 * pair. Drawing them here is also what keeps the two screens on one size,
 * the height of a value pill.
 *
 * The session names the tenant whose images are used. A currency with no
 * loaded image shows no flag and takes no space.
 *
 * @param inset How far short of the right edge the flags end, for a heading
 * whose right end holds controls.
 */
HEDGR_UI_EXPORT void draw_series_flags(const render_input& input,
    const hedgr::model::state::session_handle session, std::string_view label,
    float inset = 0.0f);

/** @brief The width @c draw_series_flags takes, zero while no flag is loaded. */
[[nodiscard]] HEDGR_UI_EXPORT float series_flags_width(
    const render_input& input, hedgr::model::state::session_handle session,
    std::string_view label);

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
 * Instances window's own toolbar and the Instances panel's pinned row both open
 * an instance's accounts, and one description of what that means is what stops
 * them from drifting.
 *
 * An instance the table does not hold is refused here rather than sent. The
 * runtime would have nothing to bind the window to, and a window bound to
 * nothing is a surface that can only report its own emptiness.
 */
HEDGR_UI_EXPORT void request_accounts_window(const render_input& input,
    std::string_view instance_id);

/**
 * @brief The session the TOOLS menu and the toolbar act on: the first one
 * signed in, or none.
 *
 * Series browsers and the command centre belong to a session, and the
 * interface has no notion of a chosen one yet.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<hedgr::model::state::session_handle>
current_session(const render_input& input);

/** @brief Asks the runtime for a window of @p kind bound to @p session. */
HEDGR_UI_EXPORT void request_session_window(const render_input& input,
    hedgr::model::state::session_handle session,
    hedgr::model::domain::window_kind kind);

/**
 * @brief Asks for the window of @p kind that shows one series of @p session.
 *
 * Chart and mini plot ids are derived from the series, so a second request for
 * a series already open focuses its window and makes no other. The series
 * browser, a chart's header and a mini plot card all open windows through this,
 * so there is one description of what opening a series means. The span is
 * what a new mini plot shows until a chart of the series sets it.
 */
HEDGR_UI_EXPORT void request_series_window(const render_input& input,
    hedgr::model::state::session_handle session,
    hedgr::model::domain::window_kind kind, std::string_view series_id,
    std::string_view label,
    std::optional<std::int64_t> span_ns = std::nullopt);

/**
 * @brief Asks for the window that shows one curve the deployment publishes.
 *
 * The window holds the curve's series and no tenor members, which is what tells
 * the read that the curve is one object the deployment states rather than a
 * ladder a client assembles from a series per tenor.
 */
HEDGR_UI_EXPORT void request_published_curve_window(const render_input& input,
    hedgr::model::state::session_handle session,
    const hedgr::model::domain::published_curve& curve);

/**
 * @brief Asks for the Instances window.
 *
 * The window is found or made by the runtime, which owns the window table. The
 * operator can close the root like any other window, and this is the request
 * that brings it back. A request for a window the table already holds sets it
 * open rather than adding a second, so the same call both raises a window on
 * screen and reopens one that was closed.
 *
 * The FILE menu's Instances row asks through this so the menu need not know
 * how a window is made.
 */
HEDGR_UI_EXPORT void request_instances_window(const render_input& input);

/**
 * @brief Raises the signal that says a shortcut was used, for the sound pack.
 *
 * The menu plays nothing itself: it raises a name and the player decides
 * whether the name has a sound (ADR 0008). The Instances panel and the Accounts
 * panel both raise it through here.
 */
HEDGR_UI_EXPORT void raise_shortcut_activated(const render_input& input,
    std::string detail);

/**
 * @brief The decoded picture of one instance, as a backend texture, or zero.
 *
 * Zero means the instance has no picture rather than that the drawing failed.
 * An instance typed by hand carries no illustration key, and a key whose asset
 * is not installed has no texture either; both are ordinary states and the
 * caller draws its own fallback for them. The keys are resolved here because
 * both the Instances panel's pinned tiles and the details pane ask the same question of the same
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
 * @brief Whether the Accounts list already holds an entry that opens this target.
 *
 * The match is on the kind and the id rather than on the derived key, for the
 * same reason the store's own removal matches on both halves: a shortcut is a
 * statement about a target, and two entries claiming one target are one fact
 * said twice.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool shortcuts_list_holds(
    const hedgr::model::state::shortcut_table& table,
    hedgr::model::domain::window_kind kind, std::string_view target_id);

/**
 * @brief The position a new entry takes, which is after every entry already on
 * the Accounts list.
 *
 * A new entry lands at the end rather than at the front, because a list that
 * rearranged itself as entries were added would be an operator's own order
 * being overwritten by their next click.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t next_shortcut_position(
    const hedgr::model::state::shortcut_table& table) noexcept;

/**
 * @brief The command that puts a target on the Accounts list, or takes it off.
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
 * @brief The save for a new instance, read from the form's drafts.
 *
 * The key is generated, so two creations never share a row even when they
 * share a name, and a rename never leaves a key a later creation can reuse.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::model::command::command
new_instance_command(const window_ui_state& state);

/** @brief Whether an instance already carries @p name, which the store holds unique. */
[[nodiscard]] HEDGR_UI_EXPORT bool instance_name_taken(
    const render_input& input, std::string_view name);

/**
 * @brief The save for the account the form holds.
 *
 * An edit keeps the row's key. A new or copied account gets a generated one,
 * and the name is the form's text as shown, with nothing appended.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::model::command::command
account_save_command(const render_input& input, const window_ui_state& state,
    std::string_view instance_id);

/**
 * @brief The command signing in as one account, with a password or without.
 *
 * An empty @p password asks the runtime for the credential the store holds;
 * anything else is the password typed for this attempt, which travels with
 * @p remember so the runtime can seal it. The Accounts window and the ACCOUNTS
 * menu both sign in through this, so the fields they fill cannot disagree about
 * which account is being opened.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::model::command::command
account_sign_in_command(std::string_view instance_id,
    std::string_view account_id, std::string_view principal,
    std::string_view password = {}, bool remember = false);

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
