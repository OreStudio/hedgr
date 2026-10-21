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
#ifndef HEDGR_SESSION_TRANSPORT_OBSERVATION_PAGE_HPP
#define HEDGR_SESSION_TRANSPORT_OBSERVATION_PAGE_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "ores.marketdata.api/domain/market_observation.hpp"
#include "ores.marketdata.api/messaging/market_observation_protocol.hpp"
#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace hedgr::session::transport {

/**
 * @brief The request for one series' newest @p limit observations.
 *
 * The by-series read orders by observation time, newest first, when no order
 * is named, so the first page is the newest window in one round trip.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT
ores::marketdata::messaging::list_by_series_id_market_observations_request
newest_observations_request(const boost::uuids::uuid& series_id,
    std::uint32_t limit);

/**
 * @brief The request for one page of a series' observations between two
 * instants, newest first.
 *
 * Both instants are inclusive and reach the deployment as microsecond text, so
 * @p from_ns is rounded down and @p to_ns up: the range may include a
 * neighbour and never loses an observation to the rounding. The order is the
 * deployment's default, observation time newest first, with the row id as the
 * tie-break, so an offset names the same row on every page.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT
ores::marketdata::messaging::list_by_series_id_market_observations_request
history_observations_request(const boost::uuids::uuid& series_id,
    std::int64_t from_ns, std::int64_t to_ns, std::uint32_t offset,
    std::uint32_t limit);

/** @brief Answers one page request, as a connection would. */
using observation_fetch = std::function<result<std::vector<
    ores::marketdata::domain::market_observation>>(
    const ores::marketdata::messaging::
        list_by_series_id_market_observations_request&)>;

/**
 * @brief Reads a range of a series by pages until it ends or the bound is hit.
 *
 * Stops at the first short page, which is the end of the range, or after
 * @p max_pages full pages, which marks the read truncated. The first failure
 * ends the read: a partial history with a hole in it is not returned.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT result<market_series_read>
read_history_pages(const observation_fetch& fetch,
    const boost::uuids::uuid& series_id, std::int64_t from_ns,
    std::int64_t to_ns, std::uint32_t page, std::uint32_t max_pages);

/**
 * @brief The plot points a page of observations carries, oldest first.
 *
 * A row whose value is not a number is skipped rather than plotted as zero.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT market_series_read
series_read_from(
    std::span<const ores::marketdata::domain::market_observation> rows);

}

#endif
