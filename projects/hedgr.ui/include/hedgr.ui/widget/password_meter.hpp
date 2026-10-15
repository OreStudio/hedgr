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
#ifndef HEDGR_UI_WIDGET_PASSWORD_METER_HPP
#define HEDGR_UI_WIDGET_PASSWORD_METER_HPP

#include "hedgr.model/domain/password_policy.hpp"
#include "hedgr.model/state/password_policy_state.hpp"
#include "hedgr.ui/export.hpp"
#include <string_view>

namespace hedgr::ui::widget {

/**
 * @brief The strength scale's own name for one level.
 *
 * Four levels and an empty one, named rather than numbered because the word is
 * what a person reads: a meter that said "3" would leave them to work out
 * whether that is good.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::string_view
strength_text(int strength) noexcept;

/**
 * @brief How many rules of @p policy @p assessment satisfies.
 *
 * The count the bar is filled from, so the bar and the list below it cannot
 * disagree about how far along the password is.
 */
[[nodiscard]] HEDGR_UI_EXPORT int
met_rule_count(const hedgr::model::domain::password_assessment& assessment,
    const hedgr::model::domain::password_policy& policy);

/**
 * @brief The rule list and the strength meter, for the password being typed.
 *
 * Drawn only where a password is being chosen. There is nothing to assess on
 * the unlock prompt, because the password there already exists and the only
 * question is whether the deployment accepts it.
 *
 * The rules come from @p state, which holds what the deployment answered. When
 * @p state is not loaded this says that strength cannot be assessed and draws
 * no list at all: a list of rules the deployment never stated would describe a
 * deployment other than this one, and drawing an empty meter would read as a
 * password that is merely weak.
 *
 * @param state what the deployment said, or why it said nothing.
 * @param password what has been typed so far.
 */
HEDGR_UI_EXPORT void
draw_password_meter(const hedgr::model::state::password_policy_state& state,
    std::string_view password);

}

#endif
