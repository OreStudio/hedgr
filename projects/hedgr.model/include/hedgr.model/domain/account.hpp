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
#ifndef HEDGR_MODEL_DOMAIN_ACCOUNT_HPP
#define HEDGR_MODEL_DOMAIN_ACCOUNT_HPP

#include "hedgr.model/export.hpp"
#include <string>

namespace hedgr::model::domain {

/**
 * @brief A login on exactly one instance.
 *
 * The same username on two instances is two accounts, which is why every row
 * names its instance rather than being found by principal alone. An account
 * carries no credential and no token: credentials live for the lifetime of a
 * session only, and the bearer token belongs to the session that obtained it.
 */
struct account final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief Key of the instance this login belongs to. */
    std::string instance_id;

    /** @brief Short human name, for example "ops read-only". */
    std::string name;

    /** @brief The principal as IAM sees it, for example user@host. */
    std::string principal;
};

}

#endif
