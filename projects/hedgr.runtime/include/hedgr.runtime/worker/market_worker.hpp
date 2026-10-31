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

#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.runtime/worker/tick_silence.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <string>

namespace hedgr::runtime::worker {

/** @brief The log line one finished market read earns, and how loud it is. */
struct market_read_report final {
    /** @brief Whether the line is a warning rather than information. */
    bool warn = false;

    /** @brief The line: the series, the request and what came back. */
    std::string text;
};

/**
 * @brief What to log for a finished market read.
 *
 * Every read says which series it was, what it asked for and how many
 * observations came back. A failure, which includes an answer that could not
 * be decoded or was refused, and an empty answer are warnings, because a chart
 * short of history is otherwise silent about why.
 *
 * @param request the read that ran.
 * @param fact the event it produced, before it is queued.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT market_read_report
report_market_read(const read_request& request, const model::event::event& fact);

/**
 * @brief Runs one market request on an open connection and fills its event.
 *
 * The connection is the session's: the catalogue is one list read, a series'
 * first load sizes its own span, and a history read asks for the range the
 * chart planned. A tick read is bounded by @c tick_read_timeout rather than by
 * the session's request timeout, because a marketdata service that is not
 * answering must not hold the session's connection for the whole of it.
 *
 * A read that went unanswered quiets the next tick read of its instance for a
 * few seconds, see @c market_silence.
 */
HEDGR_RUNTIME_EXPORT void run_market_read(
    hedgr::session::transport::session_link& link,
    const read_request& request, model::event::event& answer);

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
