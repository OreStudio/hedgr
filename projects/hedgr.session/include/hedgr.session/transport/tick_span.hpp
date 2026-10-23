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
#ifndef HEDGR_SESSION_TRANSPORT_TICK_SPAN_HPP
#define HEDGR_SESSION_TRANSPORT_TICK_SPAN_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "ores.marketdata.api/messaging/operations_protocol.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace hedgr::session::transport {

/**
 * @brief A span of time to read ticks for, in epoch nanoseconds.
 *
 * Half open, as the service reads it: a tick at @c from_ns is in, one at
 * @c to_ns is out. A zero @c to_ns states no end, which the service reads as
 * its own clock.
 */
struct tick_span final {
    std::int64_t from_ns = 0;
    std::int64_t to_ns = 0;
};

/**
 * @brief Reads one span of ticks, as one call to the deployment.
 *
 * A refusal is a failure, so a caller can tell a span that is too large from
 * one that is wrong.
 */
using tick_fetch = std::function<result<market_series_read>(const tick_span&)>;

/**
 * @brief Decodes one reply of the history call into the points of one coordinate.
 *
 * The times are the first instant plus the running sum of the gaps. A reply may
 * carry several coordinates; the points are those of the first one the reply
 * names, which is the coordinate of its earliest tick. Hedgr's rate series have
 * one, and no coordinate in the deployment is typed as a mid or a price to
 * prefer. A reply the arrays of which disagree is a protocol error.
 *
 * @return the points oldest first, or the refusal the reply states.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT result<market_series_read>
series_read_from_ticks(
    const ores::marketdata::messaging::get_series_ticks_response& reply);

/**
 * @brief Maps a refusal in a reply to its failure, named by its outcome code.
 *
 * @pre the reply's outcome is not ok.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT session_error
refusal_from(const ores::utility::domain::result& result);

/**
 * @brief Reads @p span, and once on a shorter span if the service refuses it as too large.
 *
 * Each retry keeps the end of the span and halves its length, so the newest
 * ticks, which a chart needs first, are the ones kept. The read is marked
 * truncated when a retry shortened the span or the reply held as many ticks as
 * the ceiling allows.
 *
 * @param now_ns the end of a span that states none, for the halving.
 * @param retries how many times a refusal may shorten the span.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT result<market_series_read>
read_tick_span(const tick_fetch& fetch, tick_span span, std::int64_t now_ns,
    unsigned retries);

/**
 * @brief The newest ticks of a series, at least @p min_points of them if it has them.
 *
 * Reads the narrowest of @p spans and widens to the next while the reply
 * holds fewer points than wanted, each read retrying on a span halved up
 * to three times, so a series that ticks many times a second still answers. Stops at a read the ceiling shortened, since a wider span cannot hold
 * more. A failure after a read that arrived keeps that read.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT result<market_series_read>
read_newest_ticks(const tick_fetch& fetch, std::int64_t now_ns,
    std::span<const std::int64_t> spans, std::size_t min_points);

/** @brief The first load of a series: enough ticks for a chart. */
[[nodiscard]] HEDGR_SESSION_EXPORT result<market_series_read>
read_first_load(const tick_fetch& fetch, std::int64_t now_ns);

}

#endif
