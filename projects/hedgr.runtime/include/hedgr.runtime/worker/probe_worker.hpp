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
#ifndef HEDGR_RUNTIME_WORKER_PROBE_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_PROBE_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <stop_token>

namespace hedgr::runtime::worker {

/**
 * @brief Everything a probe needs, copied so the probe never reads state.
 *
 * The instance arrives as a value, not as a handle and not as a reference into
 * a table. A probe therefore cannot read the instance row it describes, cannot
 * write it, and cannot address a row that has since been removed; it holds a
 * snapshot of the connection settings and nothing else.
 */
struct probe_job final {
    /** @brief The instance being probed, copied whole. */
    model::domain::instance instance;

    /** @brief Where to connect and how, built the way a session builds them. */
    session::transport::session_options options;
};

/**
 * @brief Probes one instance once, then reports and stops.
 *
 * The whole of a probe's life: open a connection of its own, ask the
 * deployment whether it has been bootstrapped, close the connection, and push
 * one event. It reads no state table, holds no handle, changes no session's
 * status, and shares its client with nothing -- the client is constructed
 * inside this call and destroyed before the coroutine ends, so two probes
 * cannot meet on one connection and a session cannot meet a probe on its own.
 *
 * That isolation is the point rather than a nicety. A session is an operator's
 * working connection, and a probe measures the deployment while that work is
 * open; sharing either the connection or the schedule would let a slow probe
 * delay a person's own round trip and would report the session's health when
 * the question asked was the deployment's.
 *
 * @note The probe runs on the executor that is passed in, and nowhere else.
 * The caller passes the probe pool rather than the session pool, because the
 * session pool is sized as the number of round trips an operator may have in
 * flight and a probe occupies a thread for the length of a connect timeout.
 * Spending that budget on background polling would slow down exactly the work
 * the polling is meant to leave alone.
 *
 * @pre Run on the probe executor. Never call this from the frame loop.
 * @param context the probe executor's context.
 * @param job the instance and its connection settings, by value.
 * @param events where the one result event is pushed.
 * @param stop the probe's cancellation, observed before and after the connect.
 */
HEDGR_RUNTIME_EXPORT void
spawn_probe(boost::asio::io_context& context,
            probe_job job,
            model::command::event_queue& events,
            std::shared_ptr<std::stop_source> stop);

}

#endif
