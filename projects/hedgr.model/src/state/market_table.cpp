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

namespace {

constexpr char separator = '\x1f';

[[nodiscard]] std::string session_key(std::string_view instance_id,
    std::string_view account_id) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + 1);
    key.append(instance_id);
    key.push_back(separator);
    key.append(account_id);
    return key;
}

[[nodiscard]] std::string points_key(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    std::string key = session_key(instance_id, account_id);
    key.push_back(separator);
    key.append(series_id);
    return key;
}

}

void market_table::set_catalogue(std::string_view instance_id,
    std::string_view account_id,
    std::vector<domain::market_series_row> rows) {
    auto& entry = catalogues_[session_key(instance_id, account_id)];
    entry.series = std::move(rows);
    entry.state = catalogue_state::ready;
    entry.error.clear();
}

void market_table::begin_catalogue(std::string_view instance_id,
    std::string_view account_id) {
    auto& entry = catalogues_[session_key(instance_id, account_id)];
    entry.state = catalogue_state::reading;
    entry.error.clear();
}

void market_table::fail_catalogue(std::string_view instance_id,
    std::string_view account_id, std::string message) {
    auto& entry = catalogues_[session_key(instance_id, account_id)];
    entry.state = catalogue_state::failed;
    entry.error = std::move(message);
}

const market_catalogue* market_table::catalogue(std::string_view instance_id,
    std::string_view account_id) const {
    const auto found = catalogues_.find(session_key(instance_id, account_id));
    return found == catalogues_.end() ? nullptr : &found->second;
}

void market_table::set_points(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id,
    std::vector<domain::market_point> points, std::int64_t observed_at_ns) {
    auto& snapshot = points_[points_key(instance_id, account_id, series_id)];
    snapshot.points = std::move(points);
    snapshot.observed_at_ns = observed_at_ns;
    snapshot.failure.clear();
}

void market_table::fail_points(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id,
    std::string message, std::int64_t observed_at_ns) {
    auto& snapshot = points_[points_key(instance_id, account_id, series_id)];
    snapshot.observed_at_ns = observed_at_ns;
    snapshot.failure = std::move(message);
}

const domain::market_series_snapshot* market_table::points(
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id) const {
    const auto found =
        points_.find(points_key(instance_id, account_id, series_id));
    return found == points_.end() ? nullptr : &found->second;
}

void market_table::clear() {
    catalogues_.clear();
    points_.clear();
}

}
