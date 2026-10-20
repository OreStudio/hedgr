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
#ifndef HEDGR_RUNTIME_WORKER_TICK_SINK_HPP
#define HEDGR_RUNTIME_WORKER_TICK_SINK_HPP

#include "hedgr.model/state/bucket_aggregator.hpp"
#include "hedgr.model/state/bucket_ring.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace hedgr::runtime::worker {

/**
 * @brief The watched series of one session, bucketed for the frame loop.
 *
 * The party's subscription delivers every tick of the party; this keeps the
 * ones for a watched series and drops the rest. A tick names its series by
 * canonical URI, and so does a watch, so the match is one map lookup. A watch
 * also carries the series' handle, which the aggregator stamps on every bucket
 * it closes, so the table can tell a bucket for a released series from one for
 * the series now in its slot.
 *
 * Everything a tick handler reaches lives here, so the handler co-owns it
 * through a shared pointer and a handler that outlives its stream writes into
 * memory that is still alive. The ring is single producer: the one delivery
 * thread of the one subscription. The mutex orders the watch changes from the
 * stream's thread against the ticks, and is the only lock on the tick path.
 */
class HEDGR_RUNTIME_EXPORT tick_sink final {
public:
    tick_sink() = default;

    tick_sink(const tick_sink&) = delete;
    tick_sink& operator=(const tick_sink&) = delete;

    /**
     * @brief Keeps the ticks of one series, by its canonical URI.
     *
     * Watching a series that is already watched under the same URI changes
     * nothing, so its open buckets continue. A new URI replaces the series'
     * aggregator.
     */
    void watch(model::state::series_handle series, std::string canonical_uri);

    /** @brief Drops one series' ticks from here on. */
    void unwatch(model::state::series_handle series);

    /** @brief Buckets one tick, or drops it when its series is not watched. */
    void on_tick(const session::market::tick& tick);

    /** @brief Copies the closed buckets waiting for the frame loop. */
    std::size_t drain(std::vector<model::state::series_bucket>& out) {
        return ring_.drain(out);
    }

    /** @brief Series currently watched. */
    [[nodiscard]] std::size_t watched() const;

    /** @brief Ticks bucketed since construction. */
    [[nodiscard]] std::size_t ticks() const noexcept {
        return ticks_.load(std::memory_order_relaxed);
    }

    /** @brief Buckets dropped because the frame loop was too far behind. */
    [[nodiscard]] std::size_t dropped() const noexcept {
        return ring_.dropped();
    }

private:
    model::state::bucket_ring ring_;
    mutable std::mutex mu_;

    /**
     * @brief The aggregator of each watched series, by canonical URI.
     *
     * Each aggregator holds its series' handle, which is what unwatch finds it
     * by. A watch set is hundreds of entries and changes rarely, so the scan is
     * cheaper than a second map kept in step with this one.
     */
    std::unordered_map<std::string, model::state::bucket_aggregator> series_;
    std::atomic<std::size_t> ticks_{0};
};

}

#endif
