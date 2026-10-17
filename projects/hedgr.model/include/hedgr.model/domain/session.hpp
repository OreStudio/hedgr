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
 * @brief The context a session runs in, decided once when IAM accepts it.
 *
 * The rule is the session's tenant, which is the context the deployment scopes
 * every request by. A super administrator's account belongs to the system
 * tenant, so the session acts on the deployment. Every other account acts
 * inside a tenant.
 *
 * The value is derived at the transport boundary and carried as a typed fact
 * rather than recomputed wherever a decision needs it. A window that asked
 * "does this tenant id mean administrator" would put the deployment's own rule
 * in the interface, and a change to the rule would then be found in some
 * surfaces and missed in others.
 *
 * See @c ores.iam tenant.hpp, where the system tenant's id is stated.
 */
enum class session_mode : std::uint8_t {
    /** @brief The session acts inside one tenant. */
    application = 0,
    /** @brief The session acts on the deployment, as a super administrator. */
    system_administration = 1,
};

/**
 * @brief The id the deployment gives the tenant it keeps for administrators.
 *
 * Stated here because the deployment states it in a protocol this client does
 * not receive at sign-in: the login answer carries the tenant id, and the rule
 * that says which id means "system" is the maximum UUID. It is a constant of
 * the deployment's data model rather than a setting, which is why it can be
 * written down rather than read.
 */
inline constexpr std::string_view system_tenant_id =
    "ffffffff-ffff-ffff-ffff-ffffffffffff";

/** @brief The canonical text of a mode, for logs and tooltips. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(session_mode value) noexcept;

/**
 * @brief The mode a session runs in, from the tenant IAM resolved.
 *
 * Pure, so the rule can be tested without a deployment: the system tenant is
 * the only tenant whose session acts on the deployment.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT session_mode
session_mode_for(std::string_view tenant_id) noexcept;

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
