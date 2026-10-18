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
#ifndef HEDGR_RUNTIME_WORKER_SESSION_TASK_HPP
#define HEDGR_RUNTIME_WORKER_SESSION_TASK_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <algorithm>
#include <string>
#include <utility>

namespace hedgr::runtime::worker {

/** @brief The identity and token every off-loop session read carries. */
struct session_read final {
    session::transport::session_options options;
    std::string instance_id;
    std::string account_id;
    std::string tenant_id;
    std::string token;
    std::string principal;
};

/**
 * @brief Runs one read on its own connection and posts the resulting event.
 *
 * Every image read has the same shape: connect, resume the session's token,
 * run one call, wipe the token, post. @p work fills @p fact on success; this
 * helper owns the failure sentence, the wipe and the post, so a worker only
 * names the call it makes.
 */
template<class Work>
boost::asio::awaitable<void> run_session_read(session_read job,
    model::event::event fact, Work work,
    model::command::event_queue& events) {
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    session::transport::session_client client(std::move(job.options));
    if (const auto connected = client.connect(); !connected) {
        fact.message = connected.error().message;
    } else if (const auto resumed = client.resume(job.token, job.principal);
        !resumed) {
        fact.message = resumed.error().message;
    } else {
        work(client, fact);
    }

    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    events.push(std::move(fact));
}

}

#endif
