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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_IDENTITY_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_IDENTITY_HPP

#include <string>

namespace hedgr::session::transport {

/**
 * @brief What the instance says about a signed-in principal.
 *
 * The answer to a successful sign-in: who the deployment thinks this session
 * is, and the bearer token that says so on later calls.
 *
 * @warning @c token is a secret. It is held for the session's lifetime only:
 * it is never persisted, never logged, and never copied into the state tables
 * the interface renders. It leaves this struct only as an Authorization header.
 */
struct session_identity final {
    /** @brief The bearer token presented on every authenticated request. */
    std::string token;

    /** @brief Key of the tenant the principal belongs to. */
    std::string tenant_id;

    /** @brief Human label of that tenant. */
    std::string tenant_name;

    /** @brief The party this session is scoped to, empty until one is chosen. */
    std::string party_name;

    /** @brief The principal the deployment answered with, e.g. a username. */
    std::string principal;
};

}

#endif
