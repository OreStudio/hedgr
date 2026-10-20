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
#include "hedgr.model/state/bucket_aggregator.hpp"
#include <algorithm>

namespace hedgr::model::state {

namespace {

constexpr std::array<domain::market_level, 3> levels{
    domain::market_level::fine,
    domain::market_level::medium,
    domain::market_level::coarse};

}

bucket_aggregator::bucket_aggregator(series_handle series, bucket_ring& ring)
    : series_(series)
    , ring_(ring) {
}

void bucket_aggregator::on_tick(std::int64_t at_ns, double value) {
    ++ticks_;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const auto level = levels[i];
        const auto interval = domain::market_level_ns(level);
        const auto bucket = at_ns / interval;

        if (current_[i].at_ns != 0 && at_ns < current_[i].at_ns)
            continue;

        if (bucket != bucket_start_[i]) {
            if (current_[i].at_ns != 0) {
                const auto& closed = current_[i];
                ring_.try_push(series_bucket{.series = series_,
                    .at_ns = closed.at_ns, .value = closed.value,
                    .open = closed.open, .high = closed.high,
                    .low = closed.low, .level = level});
            }
            bucket_start_[i] = bucket;
            current_[i] = series_point{.at_ns = at_ns, .value = value,
                .open = value, .high = value, .low = value};
        } else {
            current_[i].at_ns = at_ns;
            current_[i].value = value;
            current_[i].high = std::max(current_[i].high, value);
            current_[i].low = std::min(current_[i].low, value);
        }
    }
}

}
