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
#ifndef HEDGR_RUNTIME_WORKER_MARKET_STREAM_HPP
#define HEDGR_RUNTIME_WORKER_MARKET_STREAM_HPP

#include "hedgr.model/state/bucket_ring.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.runtime/worker/tick_sink.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hedgr::runtime::worker {

/** @brief What one session's tick stream needs to connect. */
struct market_stream_options final {
    session::transport::session_options options;
    std::string token;
    std::string principal;
    std::string tenant_id;
    std::string party_id;
};

/**
 * @brief One session's live prices, aggregated and handed to the frame loop.
 *
 * Owns the connection and its own thread. The frame thread posts the set of
 * series it wants and drains closed buckets; the NATS dispatch thread decodes
 * ticks and closes buckets; this class never touches the model. One instance
 * exists per authenticated session for as long as the session is live.
 *
 * A tick updates three current buckets, one per resolution. When a bucket's
 * interval passes, the previous bucket is published to the ring the frame loop
 * drains. Tick rate therefore costs the frame loop nothing; only bucket rate
 * crosses the thread boundary.
 *
 * The thread can sit inside a connect for the connect timeout, so the frame
 * thread asks it to stop with @c request_stop and joins it only once
 * @c finished says the connection is closed.
 */
class HEDGR_RUNTIME_EXPORT market_stream final : public tick_feed {
public:
    explicit market_stream(market_stream_options options);

    /** @brief Stops and joins the thread. Blocks for a connect in flight. */
    ~market_stream() override;

    market_stream(const market_stream&) = delete;
    market_stream& operator=(const market_stream&) = delete;

    /** @brief Starts the connection thread. */
    void start();

    /**
     * @brief Asks the thread to close the connection and exit.
     *
     * Returns at once. The thread closes the connection when its current
     * connect attempt, if any, returns, and @c finished then answers true.
     */
    void request_stop() override;

    /** @brief Whether the thread has closed the connection and is exiting. */
    [[nodiscard]] bool finished() const noexcept override {
        return finished_.load(std::memory_order_acquire);
    }

    /**
     * @brief The token the next connect resumes with. Thread safe.
     *
     * A refresh changes the credential and nothing else. The subscription
     * stays, and so do the open buckets.
     */
    void set_token(std::string token) override;

    /**
     * @brief Watches one series, keeping its ticks.
     *
     * Thread safe. The series is watched until @c unwatch, whether or not the
     * connection is open at the time.
     */
    void watch(std::string series_id, std::string uri) override;

    /** @brief Stops watching one series. Thread safe. */
    void unwatch(std::string_view series_id) override;

    /**
     * @brief Copies the closed buckets waiting for the frame loop.
     *
     * Called from the frame thread only.
     */
    std::size_t drain(std::vector<model::state::market_bucket>& out) override {
        return sink_->drain(out);
    }

    /**
     * @brief Whether the stream reconnected since the last call.
     *
     * The caller seeds history again for every watched series, because the
     * ticks published while the connection was down are gone.
     */
    bool take_reconnected() override;

    /** @brief Ticks bucketed since construction. */
    [[nodiscard]] std::size_t ticks() const noexcept {
        return sink_->ticks();
    }

    /** @brief Series currently watched. */
    [[nodiscard]] std::size_t watched() const {
        return sink_->watched();
    }

    /** @brief Buckets dropped because the frame loop was too far behind. */
    [[nodiscard]] std::size_t dropped() const noexcept {
        return sink_->dropped();
    }

private:
    /** @brief A watch change posted from the frame thread. */
    struct command final {
        bool add = true;
        std::string series_id;
        std::string uri;
    };

    void run();
    void stop();
    void wait_for_retry();

    market_stream_options options_;
    std::shared_ptr<tick_sink> sink_;

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<command> commands_;
    std::optional<std::string> new_token_;
    bool stopping_ = false;

    std::atomic<bool> finished_{false};
    std::atomic<bool> reconnected_{false};
};

}

#endif
