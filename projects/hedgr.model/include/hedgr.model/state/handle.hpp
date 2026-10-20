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
#ifndef HEDGR_MODEL_STATE_HANDLE_HPP
#define HEDGR_MODEL_STATE_HANDLE_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief A stale-safe reference to a row in a state table.
 *
 * The pair of index and generation replaces a pointer between state rows. A
 * released slot bumps its generation, so an outstanding handle to the previous
 * occupant compares unequal to the new one instead of silently addressing it.
 */
template <typename tag>
struct handle final {
    /** @brief The index value that means "refers to nothing". */
    static constexpr std::uint32_t invalid_index = 0xFFFFFFFFu;

    /** @brief Slot in the owning table. */
    std::uint32_t index = invalid_index;

    /** @brief Generation of the row at that slot when the handle was made. */
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index != invalid_index;
    }

    friend constexpr bool operator==(const handle&, const handle&) noexcept = default;
};

struct instance_tag {};
struct account_tag {};
struct session_tag {};
struct window_tag {};
struct folder_tag {};
struct label_tag {};
struct series_tag {};

using instance_handle = handle<instance_tag>;
using account_handle = handle<account_tag>;
using session_handle = handle<session_tag>;
using window_handle = handle<window_tag>;
using folder_handle = handle<folder_tag>;
using label_handle = handle<label_tag>;
using series_handle = handle<series_tag>;

/**
 * @brief Aliveness and generation counters shared by the live state tables.
 *
 * Rows are sparse. Releasing a slot leaves a hole, so a table's columns stay
 * addressable by index and no handle is ever renumbered. The frame loop reads
 * the aliveness byte to skip holes, which is one predictable branch per row.
 */
struct slab final {
    std::vector<std::uint8_t> alive;
    std::vector<std::uint32_t> generation;
    std::vector<std::uint32_t> free_slots;
    std::uint32_t live_count = 0;

    /** @brief Allocates a slot, reusing the most recently freed one. */
    [[nodiscard]] std::uint32_t allocate();

    /** @brief Releases a slot and invalidates every handle that referenced it. */
    bool release(std::uint32_t index) noexcept;

    /** @brief True when the slot holds a live row. */
    [[nodiscard]] bool is_alive(std::uint32_t index) const noexcept {
        return index < alive.size() && alive[index] != 0;
    }

    /** @brief Number of slots ever allocated, live and free together. */
    [[nodiscard]] std::size_t capacity() const noexcept { return alive.size(); }
};

/** @brief True when the handle refers to a live row of this slab. */
template <typename tag>
[[nodiscard]] bool owns(const slab& layout, handle<tag> h) noexcept {
    return layout.is_alive(h.index) &&
        layout.generation[h.index] == h.generation;
}

}

#endif
