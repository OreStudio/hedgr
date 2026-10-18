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
#ifndef HEDGR_UI_RENDER_CRM_RATE_HPP
#define HEDGR_UI_RENDER_CRM_RATE_HPP

#include <algorithm>
#include <cmath>

namespace hedgr::ui::render {

/** @brief A delta this small rounds to 0.00% at two decimal places. */
inline constexpr double crm_zero_delta = 0.005;

/** @brief Whether a delta should print as a neutral zero. */
[[nodiscard]] inline bool crm_delta_is_zero(double percent) {
    return std::abs(percent) < crm_zero_delta;
}

/**
 * @brief The decimal places for a CRM rate.
 *
 * A direct rate uses its pair convention. A reciprocal keeps the convention's
 * significant figures rather than reusing its decimal places, which are only
 * valid for the direct direction. Without a convention the web default of five
 * places applies.
 */
[[nodiscard]] inline int crm_decimal_places(int convention_places,
    bool reciprocal, double rate) {
    constexpr int default_places = 5;
    if (convention_places < 0)
        return default_places;
    if (!reciprocal)
        return convention_places;
    if (rate <= 0.0)
        return default_places;

    const auto order = [](double value) {
        return static_cast<int>(std::floor(std::log10(value) + 1e-9));
    };
    const int direct_order = order(1.0 / rate);
    const int significant = direct_order + 1 + convention_places;
    const int reciprocal_order = order(rate);
    return std::max(0, significant - 1 - reciprocal_order);
}

}

#endif
