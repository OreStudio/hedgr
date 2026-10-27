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
#include <boost/asio/thread_pool.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>

namespace {

using namespace std::chrono_literals;
using hedgr::runtime::worker::wait_until_or_stop;
using hedgr::runtime::worker::wait_until_or_stop_or_wake;

/** @brief Runs one wait on a pool and returns how long it took to resume. */
struct waiter final {
    boost::asio::thread_pool pool{2};
    std::shared_ptr<std::stop_source> stop = std::make_shared<std::stop_source>();
    std::shared_ptr<std::stop_source> wake = std::make_shared<std::stop_source>();
    std::promise<void> done;
    std::chrono::steady_clock::time_point started;

    void start(std::chrono::steady_clock::duration wait) {
        started = std::chrono::steady_clock::now();
        boost::asio::co_spawn(pool,
            wait_until_or_stop(started + wait, stop),
            [this](std::exception_ptr) { done.set_value(); });
    }

    void start_wakeable(std::chrono::steady_clock::duration wait) {
        started = std::chrono::steady_clock::now();
        boost::asio::co_spawn(pool,
            wait_until_or_stop_or_wake(started + wait, stop, wake),
            [this](std::exception_ptr) { done.set_value(); });
    }

    [[nodiscard]] bool finished_within(std::chrono::milliseconds limit) {
        return done.get_future().wait_for(limit) == std::future_status::ready;
    }

    ~waiter() {
        stop->request_stop();
        wake->request_stop();
        pool.stop();
        pool.join();
    }
};

}

TEST_CASE("the wait lasts until its deadline when nothing stops it", "[runtime][deadline_wait]") {
    waiter w;
    w.start(200ms);

    REQUIRE(w.finished_within(5s));
    REQUIRE(std::chrono::steady_clock::now() - w.started >= 200ms);
    REQUIRE_FALSE(w.stop->stop_requested());
}

TEST_CASE("a stop wakes the wait long before its deadline", "[runtime][deadline_wait]") {
    waiter w;
    w.start(1h);

    std::this_thread::sleep_for(100ms);
    w.stop->request_stop();

    REQUIRE(w.finished_within(2s));
    REQUIRE(std::chrono::steady_clock::now() - w.started < 10s);
}

TEST_CASE("a stop requested before the wait begins returns at once", "[runtime][deadline_wait]") {
    waiter w;
    w.stop->request_stop();
    w.start(1h);

    REQUIRE(w.finished_within(2s));
}

TEST_CASE("a wake ends the wait without stopping the session",
    "[runtime][deadline_wait]") {
    waiter w;
    w.start_wakeable(1h);

    std::this_thread::sleep_for(100ms);
    w.wake->request_stop();

    REQUIRE(w.finished_within(2s));
    REQUIRE_FALSE(w.stop->stop_requested());
}
