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
#ifndef HEDGR_MODEL_EVENT_EVENT_HPP
#define HEDGR_MODEL_EVENT_EVENT_HPP

#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>

namespace hedgr::model::event {

/**
 * @brief What happened on a worker and needs to reach the frame loop.
 *
 * Events are the only thing that crosses from a pool thread to the UI thread.
 * A worker never touches a state table; it posts an event, and the frame loop
 * applies it. That is the whole concurrency contract.
 */
enum class event_kind : std::uint8_t {
    /** @brief The session moved to a new lifecycle state. */
    session_status_changed = 0,
    /** @brief IAM accepted the account and returned a token. */
    session_authenticated = 1,
    /** @brief The attempt failed; code and message say why. */
    session_failed = 2,
    /** @brief A subscribed subject produced a payload. */
    session_data = 3,
    /** @brief A window's data was refreshed and is ready to draw. */
    window_data = 4,
    /** @brief A probe finished, whatever it found. */
    probe_result = 5,
};

/** @brief The canonical text of an event kind, for logs. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(event_kind value) noexcept;

/**
 * @brief One fact from a worker, flat and self-contained.
 *
 * The struct is deliberately wider than any single kind needs. Events are
 * occasional rather than hot, so padding them to one shape keeps the queue a
 * contiguous array of one type instead of a variant with an allocation per
 * alternative.
 */
struct event final {
    /** @brief What happened. */
    event_kind kind = event_kind::session_status_changed;

    /** @brief Instance the fact belongs to. */
    std::string instance_id;

    /** @brief Account the fact belongs to. */
    std::string account_id;

    /** @brief New status, for session_status_changed. */
    domain::session_status status = domain::session_status::disconnected;

    /** @brief Round-trip time of the request that produced the fact, in microseconds. */
    std::int64_t latency_us = 0;

    /** @brief Stable error code, for session_failed. */
    std::string code;

    /** @brief Human-readable error, for session_failed. */
    std::string message;

    /** @brief Tenant IAM resolved, for session_authenticated. */
    std::string tenant_name;

    /** @brief Party IAM resolved, for session_authenticated. */
    std::string party_name;

    /** @brief Bearer token, for session_authenticated. */
    std::string token;

    /** @brief Subject the payload arrived on, for session_data. */
    std::string subject;

    /** @brief The payload, for session_data and window_data. */
    std::string payload;

    /** @brief What the broker answered, for probe_result. */
    domain::reachability reach = domain::reachability::unknown;

    /** @brief What IAM answered, for probe_result. */
    domain::readiness ready = domain::readiness::unknown;

    /**
     * @brief The low-level cause, for probe_result.
     *
     * A field of its own rather than @c payload, because payload is documented
     * as a data payload and this is the sentence that explains an unhealthy
     * instance: the subject with no responder, the address that refused.
     */
    std::string detail;
};

}

#endif
