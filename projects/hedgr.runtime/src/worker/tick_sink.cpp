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
#include "hedgr.runtime/worker/tick_sink.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <utility>

namespace {

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.tick_sink");
    return instance;
}

}

namespace hedgr::runtime::worker {

void tick_sink::watch(model::state::series_handle series,
    std::string canonical_uri) {
    std::lock_guard lock(mu_);
    const auto held = std::ranges::find_if(series_,
        [series](const auto& entry) {
            return entry.second.series() == series;
        });
    if (held != series_.end()) {
        if (held->first == canonical_uri)
            return;
        series_.erase(held);
    }
    series_.try_emplace(std::move(canonical_uri), series, ring_);
}

void tick_sink::unwatch(model::state::series_handle series) {
    std::lock_guard lock(mu_);
    std::erase_if(series_, [series](const auto& entry) {
        return entry.second.series() == series;
    });
}

void tick_sink::on_tick(const session::market::tick& tick) {
    std::lock_guard lock(mu_);
    const auto found = series_.find(tick.series_uri);
    if (found == series_.end())
        return;

    auto& aggregator = found->second;
    const bool first = aggregator.ticks() == 0;
    aggregator.on_tick(tick.at_ns, tick.value);
    ticks_.fetch_add(1, std::memory_order_relaxed);
    if (first) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "First live tick for series '" << tick.series_uri << "' at '"
            << tick.at_ns << "'";
    }
}

std::size_t tick_sink::watched() const {
    std::lock_guard lock(mu_);
    return series_.size();
}

}
