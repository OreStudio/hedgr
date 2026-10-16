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
#include <optional>
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
 * reports setup incomplete, and service_missing when the broker answered but
 * IAM did not. The last of those is a fault rather than a gap. A deployment
 * that still needs bootstrapping answers this read, so an unanswered read is a
 * configured instance whose service is not there, and showing it as unknown
 * would read as a measurement nobody has taken. Unknown is the state before a
 * probe has reported at all, which is a different thing and stays grey.
 */
enum class readiness : std::uint8_t {
    /** @brief No probe has reported, so IAM has said nothing either way. */
    unknown = 0,
    /** @brief The broker answered but IAM did not, which is a fault. */
    service_missing = 1,
    /** @brief IAM answered that the deployment still needs setting up. */
    bootstrapping = 2,
    /** @brief IAM answered that the deployment has been set up. */
    ready = 3,
};

/**
 * @brief What a deployment answered about its own setup.
 *
 * The bootstrap read is the one question that can be asked before anybody
 * signs in, and its answer is three facts rather than one sentence. They are
 * kept as the deployment's own fields so the pane can show each on its own row
 * and so a missing fact is absent rather than the word "unknown" inside a
 * sentence nobody can copy a field out of.
 */
struct bootstrap_status final {
    /**
     * @brief Whether the deployment still needs its first administrator.
     *
     * True means setup is incomplete. It is the deployment's own answer, which
     * is the same fact its readiness is derived from.
     */
    bool in_bootstrap_mode = false;

    /** @brief Whether the deployment has a tenant somebody set up. */
    bool has_tenant = false;

    /**
     * @brief The build the answering service runs, when it stated one.
     *
     * Absent rather than empty for a deployment that named no version, so the
     * pane draws no version row rather than a blank one, which is the rule
     * every other absent field follows. The flat sentence this replaced wrote
     * the word "unknown" in its place, and an absent field carries the same
     * fact without inventing a value the deployment never sent.
     */
    std::optional<std::string> version;
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
     * @brief The deployment's own bootstrap answer, when the read came back.
     *
     * One optional struct rather than three optional fields because the three
     * are one answer: a bootstrap mode with no tenant flag beside it is a
     * reading no deployment can produce, and a shape that allowed it would make
     * the pane guess which half to trust. Presence is also what tells a
     * structured answer apart from a failure, which is a sentence and has no
     * fields at all.
     */
    std::optional<bootstrap_status> bootstrap;

    /**
     * @brief The free text that explains an unhealthy instance.
     *
     * This is where a reader finds out why an instance is unhealthy. A failure
     * that never reached the structured read gets a short headline naming what
     * is down, then the wire's own text on the next line for the ticket. A read
     * that arrived carries IAM's own remark here instead. An empty value is
     * what says there is nothing to explain.
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
