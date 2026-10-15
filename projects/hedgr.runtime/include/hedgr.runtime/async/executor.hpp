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
#ifndef HEDGR_RUNTIME_ASYNC_EXECUTOR_HPP
#define HEDGR_RUNTIME_ASYNC_EXECUTOR_HPP

#include "hedgr.runtime/export.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace hedgr::runtime::async {

/**
 * @brief A fixed pool of threads running one Asio context.
 *
 * The transport underneath Hedgr is blocking, so concurrency comes from the
 * pool rather than from the socket. A coroutine spawned here may occupy a pool
 * thread for the length of one round trip, which is what the pool exists to
 * absorb, so the pool size is the number of round trips that can be in flight
 * at once. That is a bound worth knowing rather than a detail to hide.
 */
class HEDGR_RUNTIME_EXPORT executor final {
public:
    /** @brief Starts the pool. Zero means default_thread_count(). */
    explicit executor(std::size_t thread_count = 0);

    ~executor();

    executor(const executor&) = delete;
    executor& operator=(const executor&) = delete;

    /** @brief Runs a coroutine to completion on the pool. */
    void spawn(boost::asio::awaitable<void> work);

    /** @brief Runs a plain callable on the pool. */
    void post(std::function<void()> work);

    /** @brief Stops the context and joins every thread. Safe to call twice. */
    void stop() noexcept;

    [[nodiscard]] boost::asio::io_context& context() noexcept;
    [[nodiscard]] std::size_t thread_count() const noexcept;

    /**
     * @brief The default pool size.
     *
     * Hardware concurrency clamped to the range two to four: below two a slow
     * instance stalls every session, above four the extra threads are idle
     * because the bound is the round trip, not the CPU.
     */
    [[nodiscard]] static std::size_t default_thread_count() noexcept;

private:
    std::unique_ptr<boost::asio::io_context> context_;
    std::unique_ptr<
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
        guard_;
    std::vector<std::jthread> threads_;
};

}

#endif
