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
#ifndef HEDGR_MODEL_DOMAIN_SESSION_HPP
#define HEDGR_MODEL_DOMAIN_SESSION_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>

namespace hedgr::model::domain {

/**
 * @brief The key of a session: one account presented to one instance.
 *
 * A session is keyed by the pair rather than by a surrogate id, so reconnecting
 * an account to an instance resumes the same session instead of creating a
 * second one.
 */
struct session_key final {
    /** @brief Key of the instance. */
    std::string instance_id;

    /** @brief Key of the account. */
    std::string account_id;

    friend bool operator==(const session_key&, const session_key&) = default;
};

/** @brief Where a session is in its connection lifecycle. */
enum class session_status : std::uint8_t {
    /** @brief No transport is open. */
    disconnected = 0,
    /** @brief A transport is being opened or authentication is in flight. */
    connecting = 1,
    /** @brief The transport is open and IAM accepted the account. */
    authenticated = 2,
    /** @brief The last attempt failed; last_error says why. */
    failed = 3,
};

/** @brief The number of session statuses, for indexing a lookup table. */
inline constexpr std::size_t session_status_count = 4;

/** @brief The canonical text of a status, for logs and tooltips. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(session_status value) noexcept;

/**
 * @brief A credential for one connection attempt.
 *
 * Never persisted. It exists in memory between the prompt and the connect call
 * and is cleared as soon as the attempt resolves.
 */
struct credential final {
    /** @brief The username presented to IAM. */
    std::string principal;

    /** @brief The secret presented to IAM. */
    std::string password;
};

}

#endif
