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

#include "hedgr.model/domain/identity.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/tier.hpp"
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
    /** @brief Remove an instance, its identities and their sessions. */
    remove_instance = 1,
    /** @brief Add or replace an identity, then persist it. */
    save_identity = 2,
    /** @brief Remove an identity and its session. */
    remove_identity = 3,
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

    /** @brief Identity the command concerns, when it concerns one. */
    std::string identity_id;

    /** @brief Window the command concerns, when it concerns one. */
    std::string window_id;

    /** @brief Kind of window to create, for open_window. */
    domain::window_kind new_window_kind = domain::window_kind::notice;

    /** @brief Instance row, for save_instance. */
    domain::instance instance_row;

    /** @brief Identity row, for save_identity. */
    domain::identity identity_row;

    /** @brief Principal for connect_session. Never persisted. */
    std::string principal;

    /** @brief Secret for connect_session. Never persisted, never logged. */
    std::string password;

    /** @brief Free-form detail, for a command that carries one. */
    std::string payload;
};

}

#endif
