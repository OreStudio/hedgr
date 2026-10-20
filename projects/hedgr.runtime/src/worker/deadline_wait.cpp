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
#include "hedgr.runtime/worker/deadline_wait.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

namespace hedgr::runtime::worker {

namespace {

using timer_ptr = std::shared_ptr<boost::asio::steady_timer>;

boost::asio::awaitable<void>
wait_on_strand(timer_ptr timer, std::chrono::steady_clock::time_point deadline,
               std::shared_ptr<std::stop_source> stop) {
    timer->expires_at(deadline);
    if (stop->stop_requested())
        co_return;

    boost::system::error_code ignored;
    co_await timer->async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, ignored));
}

}

boost::asio::awaitable<void>
wait_until_or_stop(std::chrono::steady_clock::time_point deadline,
                   std::shared_ptr<std::stop_source> stop) {
    /*
     * The timer is touched only from this strand, so the cancel that a stop
     * posts to it cannot race the wait it interrupts. The timer is shared
     * with the posted cancel because that handler may run after this wait has
     * already returned.
     */
    const auto strand =
        boost::asio::make_strand(co_await boost::asio::this_coro::executor);
    const auto timer = std::make_shared<boost::asio::steady_timer>(strand);

    const std::stop_callback cancel(stop->get_token(), [strand, timer] {
        boost::asio::post(strand, [timer] { timer->cancel(); });
    });

    co_await boost::asio::co_spawn(
        strand, wait_on_strand(timer, deadline, stop),
        boost::asio::use_awaitable);
}

}
