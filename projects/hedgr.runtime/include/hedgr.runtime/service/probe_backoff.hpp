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
#ifndef HEDGR_RUNTIME_SERVICE_PROBE_BACKOFF_HPP
#define HEDGR_RUNTIME_SERVICE_PROBE_BACKOFF_HPP

#include "hedgr.model/domain/probe.hpp"
#include <algorithm>
#include <chrono>

namespace hedgr::runtime::service {

/** @brief How often a deployment that answers is probed. */
inline constexpr std::chrono::milliseconds probe_cadence{5000};

/** @brief The longest a deployment that does not answer waits between probes. */
inline constexpr std::chrono::milliseconds probe_cadence_max{60000};

/**
 * @brief Whether a probe's outcome counts as the deployment having answered.
 *
 * A broker that answered and an IAM that said anything, ready or still being
 * set up, is an answer. A broker that answered while IAM did not is not: that
 * deployment is faulted and is not coming up in five seconds.
 */
[[nodiscard]] constexpr bool probe_answered(
    const hedgr::model::domain::reachability reach,
    const hedgr::model::domain::readiness ready) noexcept {
    return reach == hedgr::model::domain::reachability::reachable
        && ready != hedgr::model::domain::readiness::service_missing;
}

/**
 * @brief The wait before the next probe of a deployment.
 *
 * An answer returns to the cadence. No answer doubles the previous wait, from
 * twice the cadence the first time, up to the cap, the same shape as the
 * doubling ORE Studio's own retry wrapper uses.
 */
[[nodiscard]] constexpr std::chrono::milliseconds next_probe_interval(
    const std::chrono::milliseconds previous, const bool answered) noexcept {
    if (answered)
        return probe_cadence;
    return std::min(previous.count() == 0 ? probe_cadence * 2 : previous * 2,
        probe_cadence_max);
}

}

#endif
