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
#ifndef HEDGR_RUNTIME_WORKER_DEPLOYMENT_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_DEPLOYMENT_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <string>

namespace hedgr::runtime::worker {

/**
 * @brief Everything one deployment read needs, copied so it never reads state.
 *
 * The token is the same one the session's own worker holds, presented on a
 * second connection. That is what keeps this read off the session's transport:
 * a deployment that answers slowly costs this read its own connection rather
 * than stalling the session's refresh loop, and the session loop is what keeps
 * the workspace's latency honest.
 */
struct deployment_job final {
    /** @brief Where to connect and how. */
    session::transport::session_options options;

    /** @brief The pair this read is on behalf of, used to tag the event. */
    model::domain::session_key key;

    /** @brief The bearer token a sign-in produced. Wiped when the read ends. */
    std::string token;

    /** @brief The principal the token was issued to, for the transport. */
    std::string principal;
};

/**
 * @brief Reads one deployment and posts the snapshot as an event.
 *
 * Connects, presents the session's token, asks for the tenant counts and the
 * roster, and closes. Nothing here reads or writes a state table: the whole
 * result travels as one event, so the frame loop remains the only writer.
 *
 * The read is short-lived on purpose. A failed read is reported as a snapshot
 * carrying its own failure rather than retried here, because the retry is the
 * window's cadence and a worker that retried would hide the deployment's state
 * behind a second one.
 *
 * @pre Run on an executor thread. Never call this from the frame loop.
 */
HEDGR_RUNTIME_EXPORT void
spawn_deployment_read(boost::asio::io_context& context, deployment_job job,
                      model::command::event_queue& events);

}

#endif
