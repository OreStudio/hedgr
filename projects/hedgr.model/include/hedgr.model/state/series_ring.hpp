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
#ifndef HEDGR_MODEL_STATE_SERIES_RING_HPP
#define HEDGR_MODEL_STATE_SERIES_RING_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief One closed bucket of one level, as the ring stores it.
 *
 * The same five numbers a bucket carries on the wire, with no term and no
 * string. A zero open, high or low is a stored zero: the table does not guess.
 */
struct series_point final {
    /** @brief The bucket's time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The close. */
    double value = 0.0;

    /** @brief The first value in the bucket. */
    double open = 0.0;

    /** @brief The highest value in the bucket. */
    double high = 0.0;

    /** @brief The lowest value in the bucket. */
    double low = 0.0;

    friend constexpr bool operator==(const series_point&,
        const series_point&) noexcept = default;
};

/**
 * @brief Read-only views of the six columns of one ring.
 *
 * Every pointer addresses @c size contiguous elements, oldest first. They are
 * valid until the next call that changes the ring, so a caller uses them within
 * one frame and keeps sequence numbers, not pointers, between frames. An empty
 * ring has null pointers.
 */
struct series_columns final {
    /** @brief The time in seconds, the x axis of a plot. */
    const double* at_s = nullptr;

    /** @brief The close, the y axis of a plot. */
    const double* value = nullptr;

    /** @brief The bucket time in nanoseconds, the source of truth. */
    const std::int64_t* at_ns = nullptr;

    /** @brief The first value of each bucket. */
    const double* open = nullptr;

    /** @brief The highest value of each bucket. */
    const double* high = nullptr;

    /** @brief The lowest value of each bucket. */
    const double* low = nullptr;

    /** @brief The number of elements each column holds. */
    std::size_t size = 0;

    /**
     * @brief The sequence number of element zero.
     *
     * Every appended point has a number that never changes and never repeats,
     * so a cache that stored "the span starts at sequence S" finds its index
     * again after a compaction as S minus this.
     */
    std::uint64_t first_seq = 0;
};

/** @brief What a ring did with an appended point. */
enum class append_outcome : std::uint8_t {
    /** @brief The point was newer than the tail and was added. */
    appended = 0,
    /** @brief The point had the tail's time and replaced the tail. */
    replaced = 1,
    /** @brief The point was older than the tail and was dropped. */
    late = 2,
};

/**
 * @brief The newest points of one level, in six contiguous columns.
 *
 * A sliding window over a buffer of capacity plus 25 percent. An append writes
 * at the end. When the end reaches the buffer's size the newest points move to
 * the front, one move per column, which happens once per slack appends. Any
 * span of the newest points is therefore one pointer and a count, which a plot
 * takes without a copy. A wrapped ring cannot offer that, because ImPlot's
 * offset wraps at the count it is given.
 *
 * The buffers are allocated by the first point and grow geometrically to their
 * full size, so a ring that never sees a point costs nothing.
 *
 * Not thread safe: the frame thread owns it.
 */
class HEDGR_MODEL_EXPORT series_ring final {
public:
    /** @brief A ring that keeps at most @p capacity points. */
    explicit series_ring(std::size_t capacity);

    /**
     * @brief Adds a point, replaces the tail or drops the point.
     *
     * A point older than the newest one is dropped, because the ring runs
     * forward in time. A point with the newest one's time replaces it.
     */
    append_outcome append(const series_point& point);

    /**
     * @brief Merges a backfill into the ring by bucket.
     *
     * @p seeded is ordered by time with one point per bucket of @p bucket_ns.
     * Where a seeded bucket and a held bucket are the same bucket, the held
     * point stays, because it came from the live feed and the read may have
     * seen older prices. The newest capacity points are kept.
     *
     * A merge renumbers the ring: @c first_seq moves past every number issued
     * before it, so no stored sequence number is read as a different point.
     *
     * @return whether the points held changed.
     */
    bool merge(std::span<const series_point> seeded, std::int64_t bucket_ns);

    /** @brief The six columns. */
    [[nodiscard]] series_columns columns() const noexcept;

    /** @brief The time of the newest point, or zero when empty. */
    [[nodiscard]] std::int64_t newest_at_ns() const noexcept;

    /** @brief The number of points held. */
    [[nodiscard]] std::size_t size() const noexcept { return end_ - begin_; }

    /** @brief The most points the ring keeps. */
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /** @brief The bytes the six buffers hold, allocated and not used. */
    [[nodiscard]] std::size_t bytes() const noexcept;

private:
    void grow_or_compact();
    void write(std::size_t index, const series_point& point) noexcept;

    std::size_t capacity_;
    std::size_t total_;
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
    std::uint64_t first_seq_ = 0;
    std::vector<double> at_s_;
    std::vector<double> value_;
    std::vector<std::int64_t> at_ns_;
    std::vector<double> open_;
    std::vector<double> high_;
    std::vector<double> low_;
};

}

#endif
