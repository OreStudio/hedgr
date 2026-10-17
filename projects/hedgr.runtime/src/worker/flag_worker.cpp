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
#include "hedgr.runtime/worker/flag_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "ores.logging/make_logger.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <format>
#include <utility>

namespace {

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.flag_worker");
    return instance;
}

}

namespace hedgr::runtime::worker {

namespace {

boost::asio::awaitable<void> run_flag(flag_job job,
    model::command::event_queue& events) {
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    model::event::event fact;
    fact.kind = model::event::event_kind::flag_image;
    fact.instance_id = job.instance_id;
    fact.account_id = job.account_id;
    fact.image_code = job.code;

    session::transport::session_client client(std::move(job.options));
    if (const auto connected = client.connect(); !connected) {
        fact.message = connected.error().message;
    } else if (const auto resumed =
                   client.resume(job.token, job.principal);
        !resumed) {
        fact.message = resumed.error().message;
    } else if (const auto image = client.image_by_code(job.code); image) {
        fact.mime_type = image->mime_type;
        fact.bytes = image->data;
    } else {
        fact.message = image.error().message;
    }

    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    events.push(std::move(fact));
}

}

void spawn_flag_read(boost::asio::io_context& context, flag_job job,
    model::command::event_queue& events) {
    boost::asio::co_spawn(context, run_flag(std::move(job), events),
        boost::asio::detached);
}

}
