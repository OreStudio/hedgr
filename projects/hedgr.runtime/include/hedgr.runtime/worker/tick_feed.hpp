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
#ifndef HEDGR_RUNTIME_WORKER_TICK_FEED_HPP
#define HEDGR_RUNTIME_WORKER_TICK_FEED_HPP

#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hedgr::runtime::worker {

struct market_stream_options;

/**
 * @brief What the frame thread asks of one session's live prices.
 *
 * The seam between the manager and the stream, so a test can stand a fake in
 * for the connection. Every call is made once per frame or less, never per
 * tick: ticks go through the stream's own sink on the NATS thread and do not
 * cross this interface.
 */
class tick_feed {
public:
    virtual ~tick_feed() = default;

    /** @brief Asks the feed to close and exit, without waiting for it. */
    virtual void request_stop() = 0;

    /** @brief Whether the feed has closed and may be destroyed without a wait. */
    [[nodiscard]] virtual bool finished() const noexcept = 0;

    /** @brief The token the next resume uses. */
    virtual void set_token(std::string token) = 0;

    /**
     * @brief Watches one series, keeping its ticks.
     *
     * @p canonical_uri is the series' URI as the codec writes it, which is what
     * a tick carries. The buckets that come back name the series by @p series.
     */
    virtual void watch(model::state::series_handle series,
        std::string canonical_uri) = 0;

    /** @brief Stops watching one series. */
    virtual void unwatch(model::state::series_handle series) = 0;

    /** @brief Replaces @p out with the closed buckets waiting for the frame. */
    virtual std::size_t drain(std::vector<model::state::series_bucket>& out) = 0;

    /** @brief Whether the feed reconnected since the last call. */
    virtual bool take_reconnected() = 0;
};

/** @brief Makes and starts the feed for one authenticated session. */
using tick_feed_factory = std::function<std::unique_ptr<tick_feed>(
    market_stream_options)>;

}

#endif
