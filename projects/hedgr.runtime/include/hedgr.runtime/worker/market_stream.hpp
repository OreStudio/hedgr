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

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/state/bucket_ring.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
 */
class HEDGR_RUNTIME_EXPORT market_stream final {
public:
    explicit market_stream(market_stream_options options);
    ~market_stream();

    market_stream(const market_stream&) = delete;
    market_stream& operator=(const market_stream&) = delete;

    /** @brief Starts the connection thread. */
    void start();

    /** @brief Stops the thread and closes the connection. */
    void stop();

    /**
     * @brief Watches one series, subscribing to its ticks.
     *
     * Thread safe. The series is watched until @c unwatch, whether or not the
     * connection is open at the time.
     */
    void watch(std::string series_id, std::string uri);

    /** @brief Stops watching one series. Thread safe. */
    void unwatch(std::string_view series_id);

    /**
     * @brief Copies the closed buckets waiting for the frame loop.
     *
     * Called from the frame thread only.
     */
    std::size_t drain(std::vector<model::state::market_bucket>& out);

    /**
     * @brief Whether the stream reconnected since the last call.
     *
     * The caller seeds history again for every watched series, because the
     * ticks published while the connection was down are gone.
     */
    bool take_reconnected();

    /** @brief Ticks decoded since construction. */
    [[nodiscard]] std::size_t ticks() const noexcept {
        return ticks_.load(std::memory_order_relaxed);
    }

    /** @brief Series currently subscribed. */
    [[nodiscard]] std::size_t subscriptions() const noexcept {
        return subscriptions_.load(std::memory_order_relaxed);
    }

private:
    struct series_state;

    /** @brief A watch change posted from the frame thread. */
    struct command final {
        bool add = true;
        std::string series_id;
        std::string uri;
    };

    void run();
    void apply_commands(
        std::unordered_map<std::string, std::string>& desired,
        bool& changed);
    void wait_for_retry();

    market_stream_options options_;
    model::state::bucket_ring ring_;

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<command> commands_;
    bool stopping_ = false;

    std::atomic<std::size_t> ticks_{0};
    std::atomic<std::size_t> subscriptions_{0};
    std::atomic<bool> reconnected_{false};
};

}

#endif
