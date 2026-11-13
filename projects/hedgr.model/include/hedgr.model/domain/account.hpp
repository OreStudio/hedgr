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

    /**
     * @brief Key of the folder this account sits in.
     *
     * Inside the accounts tree of @c instance_id, which is what keeps the two
     * trees apart: an account folder is owned by the instance, and this names
     * one of them.
     */
    std::string folder_id;

    /** @brief The principal as IAM sees it, for example user@host. */
    std::string principal;

    /**
     * @brief Whether the local store holds a credential for this account.
     *
     * A fact about the local store rather than about the deployment, and it is
     * not persisted: the store answers it when the accounts are loaded, and a
     * save never writes it. The interface reads it to decide whether Sign in
     * can use a remembered password or has to ask for one.
     */
    bool has_credential = false;

    /**
     * @brief The party this account last acted as, or empty for none yet.
     *
     * A login may act as more than one party, and the deployment's answer
     * usually lands on a default that is not the desk's own - a system party,
     * say. The party the operator chose is remembered here so the next sign-in
     * starts in it, and so a party prompt only appears when there is no choice
     * to repeat. Empty means nothing has been chosen; it is written when a
     * session acting as this account switches party.
     */
    std::string party_id;
};

}

#endif
