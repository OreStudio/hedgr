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

#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/curve_table.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include <cstdint>
#include <span>
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
 * @brief Reads a curve's instants into the model's shape.
 *
 * The deployment publishes one set per instant, and a set names only the
 * pillars it carried, so the instants are put in time order and the pillars
 * are the union of every set's. A set that omits a pillar carries the latest
 * earlier value forward: a curve is the object as it stood, and a pillar that
 * was not republished did not stop existing. A pillar with no value at or
 * before an instant is stored as not-a-number, which a plot draws as a gap
 * rather than as a rate of zero.
 *
 * A pillar whose value the deployment could not spell as a number is left out,
 * because a zero on a curve is a rate of zero and that is a lie a chart draws.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT curve_reading read_published_curves(
    std::span<const hedgr::session::transport::curve_instant> sets);

/**
 * @brief Turns a curve event into the model's row.
 *
 * The event is already columnar, so this is a move rather than a parse: the
 * actor that read the curve paid the boundary cost on its own thread, which is
 * where the frame loop must never pay it.
 *
 * @param event the @c curve_sets event. A non-empty @c message is a failed read
 *        and is recorded as one, keeping whatever the row already held.
 * @return whether the event carried a curve.
 */
HEDGR_RUNTIME_EXPORT bool apply_curve_event(
    model::state::curve_table& curves, model::state::curve_handle handle,
    const hedgr::model::event::event& event);

}

#endif
