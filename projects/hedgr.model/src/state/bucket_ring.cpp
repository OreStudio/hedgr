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
#include "hedgr.model/state/bucket_ring.hpp"
#include <algorithm>

namespace hedgr::model::state {

bool bucket_ring::try_push(market_bucket bucket) {
    const auto released = released_.load(std::memory_order_acquire);
    if (next_ - released >= capacity) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    slots_[next_ % capacity] = std::move(bucket);
    ++next_;
    published_.store(next_, std::memory_order_release);
    return true;
}

std::size_t bucket_ring::drain(std::vector<market_bucket>& out,
    std::size_t limit) {
    const auto published = published_.load(std::memory_order_acquire);
    const auto available = published - taken_;
    const auto take = std::min<std::uint64_t>(available,
        std::min<std::size_t>(limit, capacity));
    out.clear();
    out.reserve(out.size() + take);
    for (std::uint64_t i = 0; i < take; ++i) {
        out.push_back(std::move(slots_[(taken_ + i) % capacity]));
    }
    taken_ += take;
    released_.store(taken_, std::memory_order_release);
    high_water_ = std::max(high_water_,
        static_cast<std::size_t>(published - (taken_ - take)));
    return static_cast<std::size_t>(take);
}

}
