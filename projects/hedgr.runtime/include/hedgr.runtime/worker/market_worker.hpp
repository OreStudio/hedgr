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
#ifndef HEDGR_RUNTIME_WORKER_MARKET_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_MARKET_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/tick_silence.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <string>

namespace hedgr::runtime::worker {

/**
 * @brief One market read: the series catalogue, or one series' ticks.
 *
 * The read runs on its own connection, presenting the session's token, so a
 * slow marketdata service costs its own screen its own request rather than
 * stalling the session's refresh loop.
 */
struct market_job final {
    /** @brief Where to connect and how. */
    session::transport::session_options options;

    /** @brief The pair this read is on behalf of. */
    model::domain::session_key key;

    /** @brief The bearer token a sign-in produced. Wiped when the read ends. */
    std::string token;

    /** @brief The principal the token was issued to. */
    std::string principal;

    /** @brief Whether this is the catalogue read rather than one series. */
    bool catalogue = false;

    /** @brief The series to read, for a points read. */
    std::string series_id;

    /**
     * @brief The typed identity the catalogue read gave this series.
     *
     * A tick read names its series by this, so the read does not also list the
     * deployment's identities. Empty when the catalogue did not carry one, and
     * the read then falls back to the list.
     */
    model::domain::market_series_identity identity;

    /**
     * @brief Whether this reads a time range of the series, not its first load.
     *
     * A history read asks for the ticks from @c history_from_ns to
     * @c history_to_ns in one call, retrying once on a shorter span when the
     * deployment refuses it as too large, and answers with @c series_history.
     * The first load, which answers with @c series_points, sizes its own span.
     */
    bool history = false;

    /** @brief The earliest instant a history read wants, epoch nanoseconds. */
    std::int64_t history_from_ns = 0;

    /** @brief The latest instant a history read wants, epoch nanoseconds. */
    std::int64_t history_to_ns = 0;
};

/**
 * @brief Runs one market read and posts its outcome as one event.
 *
 * @pre Run on an executor thread. Never call this from the frame loop.
 */
/** @brief The log line one finished market read earns, and how loud it is. */
struct market_read_report final {
    /** @brief Whether the line is a warning rather than information. */
    bool warn = false;

    /** @brief The line: the series, the request and what came back. */
    std::string text;
};

/**
 * @brief What to log for a finished read.
 *
 * Every read says which series it was, what it asked for and how many
 * observations came back. A failure, which includes an answer that could not
 * be decoded or was refused, and an empty answer are warnings, because a chart
 * short of history is otherwise silent about why.
 *
 * @param job the read that ran.
 * @param fact the event it produced, before it is queued.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT market_read_report report_market_read(
    const market_job& job, const model::event::event& fact);

HEDGR_RUNTIME_EXPORT void
spawn_market_read(boost::asio::io_context& context, market_job job,
                  model::command::event_queue& events);

/**
 * @brief The process-wide tick-silence window every read job shares.
 *
 * A read that went unanswered quiets the next tick read of its instance for a
 * few seconds. Exposed so a test can put an instance inside the window and see
 * the read that follows report the wait rather than ask again.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT tick_silence& market_silence();

}

#endif
