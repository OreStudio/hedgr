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
#ifndef HEDGR_MODEL_COMMAND_COMMAND_HPP
#define HEDGR_MODEL_COMMAND_COMMAND_HPP

#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::model::command {

/**
 * @brief Something the interface asks the runtime to do.
 *
 * The interface never mutates state. It pushes a command and the runtime
 * applies it, which is what keeps rendering free of side effects and the state
 * tables owned by a single writer.
 */
enum class command_kind : std::uint8_t {
    /** @brief Add or replace an instance, then persist it. */
    save_instance = 0,
    /** @brief Remove an instance, its accounts and their sessions. */
    remove_instance = 1,
    /** @brief Add or replace an account, then persist it. */
    save_account = 2,
    /** @brief Remove an account and its session. */
    remove_account = 3,
    /** @brief Open a session for a pair, authenticating with a credential. */
    connect_session = 4,
    /** @brief Close a session. */
    disconnect_session = 5,
    /** @brief Create a window bound to a session. */
    open_window = 6,
    /** @brief Close and forget a window. */
    close_window = 7,
    /** @brief Ask a window's session for fresh data now. */
    refresh_window = 8,
    /** @brief Stop every worker and leave the frame loop. */
    shutdown = 9,
    /** @brief Add or replace a folder, then persist it. */
    save_folder = 10,
    /** @brief Remove a folder and everything below it. */
    remove_folder = 11,
    /** @brief Add or replace a label, then persist it. */
    save_label = 12,
    /** @brief Remove a label and every attachment of it. */
    remove_label = 13,
    /** @brief Attach a label to an instance or an account. */
    attach_label = 14,
    /** @brief Detach a label from an instance or an account. */
    detach_label = 15,
    /** @brief Probe one instance now, rather than at its next deadline. */
    probe_instance = 16,
    /** @brief Probe every instance now. */
    probe_all = 17,
    /**
     * @brief Seal the store under a new master password, then hold it.
     *
     * The password travels in @c password, the same field every other secret
     * crosses in, and the runtime wipes it as soon as the store is sealed.
     */
    set_master_password = 18,
    /**
     * @brief Derive the key from @c password and verify it against the store.
     *
     * Slow on purpose and therefore answered by an event rather than by the
     * time this returns: @c store_unlocked or @c store_lock_failed.
     */
    unlock_store = 19,
    /** @brief Drop the held password. The store returns to locked. */
    lock_store = 20,
    /** @brief Add or replace a shortcuts bar entry, then persist it. */
    save_shortcut = 21,
    /** @brief Remove a shortcuts bar entry. */
    remove_shortcut = 22,
    /**
     * @brief Store a credential that was just sealed for an account.
     *
     * The sealing is the slow part and happens on a pool thread. This command
     * carries the sealed value back to the frame loop, which is the only thread
     * that writes the store.
     */
    store_credential = 23,
};

/** @brief The canonical text of a command kind, for logs. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(command_kind value) noexcept;

/**
 * @brief One request from the interface to the runtime.
 *
 * A credential travels in a command and nowhere else. The runtime clears the
 * password as soon as the connect attempt resolves, and neither field is ever
 * logged or persisted.
 */
struct command final {
    /** @brief What to do. */
    command_kind kind = command_kind::refresh_window;

    /** @brief Instance the command concerns, when it concerns one. */
    std::string instance_id;

    /** @brief Account the command concerns, when it concerns one. */
    std::string account_id;

    /** @brief Window the command concerns, when it concerns one. */
    std::string window_id;

    /** @brief Kind of window to create, for open_window. */
    domain::window_kind new_window_kind = domain::window_kind::notice;

    /** @brief Instance row, for save_instance. */
    domain::instance instance_row;

    /** @brief Account row, for save_account. */
    domain::account account_row;

    /** @brief Folder row, for save_folder. */
    domain::folder folder_row;

    /** @brief Label row, for save_label. */
    domain::label label_row;

    /**
     * @brief The attachment, for attach_label and detach_label.
     *
     * The junction names the owner by kind and id, so the kind travels here
     * rather than the runtime inferring it from which id field happens to be
     * set: an instance and an account can carry the same id, and guessing would
     * attach the label to the wrong one.
     */
    domain::labelling labelling_row;

    /** @brief Shortcuts bar entry, for save_shortcut and remove_shortcut. */
    domain::shortcut shortcut_row;

    /** @brief Principal for connect_session. Never persisted. */
    std::string principal;

    /** @brief Secret for connect_session. Never persisted, never logged. */
    std::string password;

    /**
     * @brief Whether a typed password should also be remembered.
     *
     * For connect_session. The credential is sealed under the master password
     * and written on a pool thread, so the next sign-in can use it without
     * asking. It is stored whether or not the deployment accepts it, because
     * the operator typed it as the account's password and checking it is what
     * the sign-in attempt is for.
     */
    bool remember = false;

    /** @brief Market series a chart window plots, for open_window. */
    std::string series_id;

    /** @brief Human label of that series, for the window's title. */
    std::string series_label;

    /** @brief Free-form detail, for a command that carries one. */
    std::string payload;
};

}

#endif
