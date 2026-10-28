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

#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <cstdint>
#include <string_view>

namespace hedgr::runtime::worker {

/**
 * @brief Turns one CRM batch answer into the event's fields.
 *
 * The mapping is driven against a fake link: the cells, their newest as-of,
 * the latency and the conventions are all filled here, and a failed batch
 * becomes the event's message.
 */
HEDGR_RUNTIME_EXPORT void run_crm_read(
    hedgr::session::transport::session_link& link, std::string_view party,
    std::string_view crm_name, bool reciprocal,
    model::event::event& answer);

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
