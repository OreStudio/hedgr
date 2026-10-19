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
#include "hedgr.session/transport/observation_page.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include "hedgr.session/market/series_label.hpp"

namespace hedgr::session::transport {

ores::marketdata::messaging::list_by_series_id_market_observations_request
newest_observations_request(const boost::uuids::uuid& series_id,
    std::uint32_t limit) {
    ores::marketdata::messaging::list_by_series_id_market_observations_request
        r{};
    r.series_id = series_id;
    r.offset = 0;
    r.limit = limit;
    return r;
}

market_series_read series_read_from(
    std::span<const ores::marketdata::domain::market_observation> rows) {
    market_series_read r;
    r.points.reserve(rows.size());
    for (const auto& row : rows) {
        double value = 0.0;
        try {
            value = std::stod(row.value);
        } catch (const std::exception&) {
            continue;
        }
        const auto at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            row.observation_datetime.time_since_epoch())
                               .count();
        r.points.push_back(market_point{.at_ns = at_ns,
            .value = value,
            .term = market::series_term(row.oresmd_uri)});
    }
    std::ranges::sort(r.points, {}, &market_point::at_ns);
    return r;
}

}
