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
#include "hedgr.model/state/market_table.hpp"
#include <utility>

namespace hedgr::model::state {

void market_table::set_catalogue(std::vector<domain::market_series_row> rows) {
    series_ = std::move(rows);
    catalogue_ = catalogue_state::ready;
    catalogue_error_.clear();
}

void market_table::begin_catalogue() {
    catalogue_ = catalogue_state::reading;
    catalogue_error_.clear();
}

void market_table::fail_catalogue(std::string message) {
    catalogue_ = catalogue_state::failed;
    catalogue_error_ = std::move(message);
}

void market_table::set_points(std::string_view series_id,
    std::vector<domain::market_point> points, std::int64_t observed_at_ns) {
    auto& snapshot = points_[std::string{series_id}];
    snapshot.points = std::move(points);
    snapshot.observed_at_ns = observed_at_ns;
    snapshot.failure.clear();
}

void market_table::fail_points(std::string_view series_id,
    std::string message, std::int64_t observed_at_ns) {
    auto& snapshot = points_[std::string{series_id}];
    snapshot.observed_at_ns = observed_at_ns;
    snapshot.failure = std::move(message);
}

const domain::market_series_snapshot*
market_table::points(std::string_view series_id) const {
    const auto found = points_.find(std::string{series_id});
    return found == points_.end() ? nullptr : &found->second;
}

void market_table::clear() {
    series_.clear();
    catalogue_ = catalogue_state::unread;
    catalogue_error_.clear();
    points_.clear();
}

}
