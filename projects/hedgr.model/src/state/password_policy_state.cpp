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
#include "hedgr.model/state/password_policy_state.hpp"

namespace hedgr::model::state {

void password_policy_state::begin() {
    state_ = domain::policy_state::loading;
    policy_ = domain::password_policy{};
    detail_.clear();
}

void password_policy_state::set(domain::password_policy answer) {
    state_ = domain::policy_state::loaded;
    policy_ = std::move(answer);
    detail_.clear();
}

void password_policy_state::fail(std::string why) {
    /*
     * The policy is cleared as well as the state, so an answer that replaced a
     * previous one cannot be left behind for a screen that reads the policy
     * without checking the state first. There is nothing to assess against
     * here, and a stale rule set is worse than none.
     */
    state_ = domain::policy_state::unavailable;
    policy_ = domain::password_policy{};
    detail_ = std::move(why);
}

}
