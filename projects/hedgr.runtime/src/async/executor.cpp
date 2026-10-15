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
#include "hedgr.runtime/async/executor.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/system_error.hpp>
#include <exception>
#include <utility>

namespace hedgr::runtime::async {

std::size_t executor::default_thread_count() noexcept {
    const auto reported = static_cast<std::size_t>(std::thread::hardware_concurrency());
    if (reported == 0)
        return 2;
    return std::clamp<std::size_t>(reported, 2, 4);
}

executor::executor(std::size_t thread_count)
    : context_(std::make_unique<boost::asio::io_context>()),
      guard_(std::make_unique<
             boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
          boost::asio::make_work_guard(*context_))) {
    /*
     * The guard is what keeps run() from returning the moment the queue is
     * empty: at construction no work has been posted yet, so without it every
     * thread would exit immediately and spawn() would queue work nobody runs.
     */
    const auto count = thread_count == 0 ? default_thread_count() : thread_count;
    threads_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        threads_.emplace_back([this] {
            try {
                context_->run();
            } catch (const boost::system::system_error&) {
                /*
                 * run() rethrows the error of a handler that failed. A handler
                 * reports its own failure through the event channel, so by the
                 * time it surfaces here there is nothing left to tell anyone,
                 * and the pool is on its way down.
                 */
            } catch (const std::exception&) {
                /*
                 * Nothing may escape a thread body: an exception reaching the
                 * end of it calls std::terminate and takes the frame loop down
                 * with it, which is a far worse outcome than one lost handler.
                 */
            }
        });
    }
}

executor::~executor() {
    stop();
}

void executor::spawn(boost::asio::awaitable<void> work) {
    boost::asio::co_spawn(*context_, std::move(work), boost::asio::detached);
}

void executor::post(std::function<void()> work) {
    boost::asio::post(*context_, std::move(work));
}

void executor::stop() noexcept {
    if (!context_)
        return;

    /*
     * The guard goes first: run() cannot return while work is still being
     * counted, so releasing it and then stopping is what lets every thread come
     * back to the join below.
     */
    guard_->reset();
    context_->stop();
    for (auto& thread : threads_) {
        if (thread.joinable())
            thread.join();
    }
    threads_.clear();
}

boost::asio::io_context& executor::context() noexcept {
    return *context_;
}

std::size_t executor::thread_count() const noexcept {
    return threads_.size();
}

}
