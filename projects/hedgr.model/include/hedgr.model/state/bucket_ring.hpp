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
#ifndef HEDGR_MODEL_STATE_BUCKET_RING_HPP
#define HEDGR_MODEL_STATE_BUCKET_RING_HPP

#include "hedgr.model/export.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief A fixed ring between the NATS thread and the frame loop.
 *
 * Single producer, single consumer. The producer is the dispatch thread of one
 * session's connection, the consumer is the frame loop. Slots are preallocated,
 * the cursors sit on separate cache lines, and publication is one release store
 * per bucket. A full ring drops the newest bucket rather than blocking the
 * producer: a market tick is worth less than the dispatch thread's progress,
 * and the chart shows the points it did receive.
 *
 * The producer and consumer must not be the same thread, and there must be
 * exactly one of each while the ring is in use.
 */
class HEDGR_MODEL_EXPORT bucket_ring final {
public:
    /** @brief The most buckets the ring holds before it starts dropping. */
    static constexpr std::size_t capacity = 4096;

    bucket_ring() = default;

    bucket_ring(const bucket_ring&) = delete;
    bucket_ring& operator=(const bucket_ring&) = delete;

    /**
     * @brief Adds one bucket, or drops it when the ring is full.
     * @return whether the bucket was published.
     */
    bool try_push(series_bucket bucket);

    /**
     * @brief Copies the published buckets into @p out, oldest first.
     *
     * At most @p limit buckets are taken, so a frame that falls behind cannot
     * spend an unbounded amount of time draining.
     */
    std::size_t drain(std::vector<series_bucket>& out,
        std::size_t limit = capacity);

    /** @brief Buckets dropped because the ring was full, since construction. */
    [[nodiscard]] std::size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /** @brief The most buckets ever waiting to be consumed. */
    [[nodiscard]] std::size_t high_water() const noexcept {
        return high_water_;
    }

private:
    std::array<series_bucket, capacity> slots_{};

    /* One cursor per cache line: the producer and consumer never share one. */
    alignas(64) std::atomic<std::uint64_t> published_{0};
    alignas(64) std::atomic<std::uint64_t> released_{0};
    alignas(64) std::atomic<std::size_t> dropped_{0};

    /* Producer state, touched only by the producer. */
    std::uint64_t next_{0};

    /* Consumer state, touched only by the consumer. */
    std::uint64_t taken_{0};
    std::size_t high_water_{0};
};

}

#endif
