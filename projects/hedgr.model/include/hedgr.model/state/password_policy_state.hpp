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
#ifndef HEDGR_MODEL_STATE_PASSWORD_POLICY_STATE_HPP
#define HEDGR_MODEL_STATE_PASSWORD_POLICY_STATE_HPP

#include "hedgr.model/domain/password_policy.hpp"
#include "hedgr.model/export.hpp"
#include <string>
#include <utility>

namespace hedgr::model::state {

/**
 * @brief What one deployment said when it was asked for its password policy.
 *
 * One record and not a table, because the question is asked of the deployment
 * the process is pointed at and not of the catalogue: the create prompt is the
 * first surface an operator meets, it is drawn while the store is still shut,
 * and there is no instance row to read at that moment. Whatever the deployment
 * answers is therefore the policy of the deployment being set up.
 *
 * The state travels with the policy because the screen has to tell "not yet
 * asked" from "asked and not answered". A policy of zeroes drawn as though the
 * deployment had stated it would require nothing, which is a claim; the truth
 * is that nothing was stated, and a person is owed the difference.
 */
class HEDGR_MODEL_EXPORT password_policy_state final {
public:
    /**
     * @brief Records that the read has been asked for and has not answered.
     *
     * The starting state, so a screen drawn before the answer arrives says it
     * is waiting rather than that there is no policy.
     */
    void begin();

    /** @brief Records what @p answer said, as the policy in force. */
    void set(domain::password_policy answer);

    /**
     * @brief Records that there is no policy to assess against.
     *
     * @p why is the reason, for the tooltip: it names the deployment that did
     * not answer, which is the one thing a person can act on.
     */
    void fail(std::string why);

    /** @brief Where the read stands. */
    [[nodiscard]] domain::policy_state state() const noexcept { return state_; }

    /**
     * @brief The deployment's policy.
     *
     * Only meaningful when @c state is @c loaded. Read it through
     * @c is_loaded rather than by comparing fields, because an unanswered
     * policy and a permissive one look the same field by field.
     */
    [[nodiscard]] const domain::password_policy& policy() const noexcept {
        return policy_;
    }

    /** @brief Whether a policy has been read and is in force. */
    [[nodiscard]] bool is_loaded() const noexcept {
        return state_ == domain::policy_state::loaded;
    }

    /** @brief Why there is no policy, or empty when there is one. */
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

private:
    domain::policy_state state_ = domain::policy_state::loading;
    domain::password_policy policy_;
    std::string detail_;
};

}

#endif
