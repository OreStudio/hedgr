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
#ifndef HEDGR_MODEL_DOMAIN_PROBE_HPP
#define HEDGR_MODEL_DOMAIN_PROBE_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::model::domain {

/**
 * @brief What the last probe found about the broker.
 *
 * Red when no connection could be opened, green when one could. The third
 * state is not a shade of either: it is what is known before a probe has run,
 * or after one whose answer never arrived, and a dot that guessed between the
 * other two would be reporting a measurement nobody took.
 */
enum class reachability : std::uint8_t {
    /** @brief No probe has reported, so nothing is known about the broker. */
    unknown = 0,
    /** @brief No connection could be opened to the broker. */
    unreachable = 1,
    /** @brief A connection to the broker was opened. */
    reachable = 2,
};

/**
 * @brief What the last probe found about IAM.
 *
 * Ready when IAM reports the deployment bootstrapped, bootstrapping when IAM
 * reports setup incomplete, and unknown when the broker answered but IAM did
 * not. The last of those is the one worth stating plainly: a deployment whose
 * IAM is down has not said it is bootstrapping, it has said nothing. Reporting
 * it as bootstrapping would turn an outage into a work item for whoever reads
 * the list, which is a different and wrong instruction.
 */
enum class readiness : std::uint8_t {
    /** @brief IAM did not answer, so it has said nothing either way. */
    unknown = 0,
    /** @brief IAM answered that the deployment still needs setting up. */
    bootstrapping = 1,
    /** @brief IAM answered that the deployment has been set up. */
    ready = 2,
};

/**
 * @brief What one probe found, and when.
 *
 * A fact about a moment rather than a property of the instance, which is why
 * it is never written into the instance row and never persisted: the row is
 * configuration and this is a measurement of it.
 */
struct probe_result final {
    /** @brief What the broker answered, at its coarsest. */
    reachability reach = reachability::unknown;

    /** @brief What IAM answered, when IAM answered at all. */
    readiness ready = readiness::unknown;

    /** @brief How long the attempt took, in microseconds. */
    std::int64_t latency_us = 0;

    /** @brief When the attempt finished, on the monotonic clock. */
    std::int64_t observed_at_ns = 0;

    /**
     * @brief The low-level cause, for a tooltip.
     *
     * This is where a reader finds out why an instance is unhealthy: the
     * subject that had no responder, the address that refused, the sentence
     * IAM sent. Empty when there is nothing to explain.
     */
    std::string detail;
};

/** @brief The canonical text of a reachability, for logs and tests. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(reachability value) noexcept;

/** @brief The canonical text of a readiness, for logs and tests. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(readiness value) noexcept;

}

#endif
