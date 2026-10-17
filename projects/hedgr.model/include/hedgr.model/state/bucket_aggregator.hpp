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
#ifndef HEDGR_MODEL_STATE_BUCKET_AGGREGATOR_HPP
#define HEDGR_MODEL_STATE_BUCKET_AGGREGATOR_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/bucket_ring.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hedgr::model::state {

/**
 * @brief Turns one series' ticks into the three resolutions the chart reads.
 *
 * The aggregator holds one current bucket per level and publishes the previous
 * bucket to the ring when an interval passes. A tick within a bucket replaces
 * the value, so a one second bucket costs one slot however many ticks it saw.
 * A tick older than what a level already holds is dropped: the rings run
 * forward in time.
 *
 * Not thread safe. The stream that owns it serialises ticks with a mutex, which
 * is the only lock on the tick path.
 */
class HEDGR_MODEL_EXPORT bucket_aggregator final {
public:
    bucket_aggregator(std::string series_id, bucket_ring& ring);

    /** @brief Consumes one tick, publishing any bucket it closes. */
    void on_tick(std::int64_t at_ns, double value);

    /** @brief Ticks consumed since construction. */
    [[nodiscard]] std::size_t ticks() const noexcept {
        return ticks_;
    }

private:
    std::string series_id_;
    bucket_ring& ring_;
    std::array<std::int64_t, 3> bucket_start_{-1, -1, -1};
    std::array<domain::market_point, 3> current_{};
    std::size_t ticks_ = 0;
};

}

#endif
