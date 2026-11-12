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
#ifndef HEDGR_RUNTIME_SERVICE_CURVE_MAPPING_HPP
#define HEDGR_RUNTIME_SERVICE_CURVE_MAPPING_HPP

#include "hedgr.model/state/curve_table.hpp"
#include "hedgr.runtime/export.hpp"
#include "ores.marketdata.api/messaging/published_curves_operations_protocol.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace hedgr::runtime::service {

/** @brief One published curve, as the model stores it. */
struct curve_reading final {
    std::string label;

    /** @brief The pillars, in the order the deployment sent them. */
    std::vector<std::string> pillars;

    /** @brief One instant per row of @c values, oldest first. */
    std::vector<std::int64_t> as_of_ns;

    /** @brief Row-major, pillars per instant, in the deployment's order. */
    std::vector<double> values;
};

/**
 * @brief Reads one published curve set into the model's shape.
 *
 * Pillars arrive as text ("10Y", "3M") and values as decimal text, so both are
 * parsed here, at the boundary, and never again: the model orders the pillars
 * once and stores the values as numbers. A pillar whose value will not parse is
 * left out rather than stored as a zero, because a zero on a curve is a rate of
 * zero, which is a lie a chart draws.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT curve_reading
read_published_curve(
    const ores::marketdata::messaging::published_curve_set& set);

/**
 * @brief Stores a curve set on the row a window holds.
 *
 * @return whether the set had a usable point.
 */
HEDGR_RUNTIME_EXPORT bool apply_published_curve(
    model::state::curve_table& curves, model::state::curve_handle handle,
    const ores::marketdata::messaging::published_curve_set& set);

}

#endif
