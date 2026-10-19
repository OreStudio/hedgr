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
#ifndef HEDGR_RUNTIME_WORKER_CRM_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_CRM_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/session_task.hpp"
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::runtime::worker {

/** @brief One read of a party's cross-rates batch. */
struct crm_job final {
    /** @brief The signed-in session the read runs under. */
    session_read session;

    /** @brief The party whose matrix is read. */
    std::string party_id;

    /** @brief One CRM to scope to, or empty for every enabled CRM. */
    std::string crm_name;

    /** @brief Whether to ask the deployment for reciprocal backfill. */
    bool reciprocal = false;
};

/**
 * @brief Reads one CRM batch and posts its cells as a crm_rates event.
 *
 * The read is one get_crm_rates call on a worker connection. The runtime's
 * pump decides when it runs, once per batch however many windows show it.
 */
HEDGR_RUNTIME_EXPORT void spawn_crm_read(boost::asio::io_context& context,
    crm_job job, model::command::event_queue& events);

/**
 * @brief Reads an ISO-8601 UTC instant, or returns zero when it cannot.
 *
 * The CRM service reports its observation times as text. This is where that
 * text becomes nanoseconds, once per batch, so no screen parses it per frame.
 * The grammar is OreStudio's wire shape: either separator, an optional
 * fraction, and a UTC designator. A non-zero offset or a missing designator
 * reads as zero rather than as the wrong instant.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT std::int64_t parse_iso8601_utc(
    std::string_view text);

}

#endif
