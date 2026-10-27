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
#ifndef HEDGR_RUNTIME_WORKER_DEADLINE_WAIT_HPP
#define HEDGR_RUNTIME_WORKER_DEADLINE_WAIT_HPP

#include "hedgr.runtime/export.hpp"
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <memory>
#include <stop_token>

namespace hedgr::runtime::worker {

/**
 * @brief Suspends until a deadline passes or a stop is requested.
 *
 * A stop wakes the wait at once, from whichever thread requests it, so a
 * disconnect never waits out the deadline. Check the stop source on return to
 * tell the two apart.
 */
HEDGR_RUNTIME_EXPORT boost::asio::awaitable<void>
wait_until_or_stop(std::chrono::steady_clock::time_point deadline,
                   std::shared_ptr<std::stop_source> stop);

/**
 * @brief Suspends until a deadline passes, a stop is requested, or @p wake is.
 *
 * The same wait with a second thing that may end it, for work that has to be
 * applied promptly while the session is otherwise idle: a party change does not
 * wait out the token's life. Check both sources on return; @p stop means the
 * session is ending, @p wake means there is work waiting.
 */
HEDGR_RUNTIME_EXPORT boost::asio::awaitable<void>
wait_until_or_stop_or_wake(std::chrono::steady_clock::time_point deadline,
                           std::shared_ptr<std::stop_source> stop,
                           std::shared_ptr<std::stop_source> wake);

}

#endif
