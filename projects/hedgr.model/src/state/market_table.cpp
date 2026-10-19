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
#include <algorithm>
#include <map>
#include <utility>

namespace hedgr::model::state {

void market_table::set_catalogue(std::string_view instance_id,
    std::string_view account_id,
    std::vector<domain::market_series_row> rows) {
    auto& entry = slot_in(catalogues_, {instance_id, account_id});
    entry.series = std::move(rows);
    entry.state = catalogue_state::ready;
    entry.error.clear();
}

void market_table::begin_catalogue(std::string_view instance_id,
    std::string_view account_id) {
    auto& entry = slot_in(catalogues_, {instance_id, account_id});
    entry.state = catalogue_state::reading;
    entry.error.clear();
}

void market_table::fail_catalogue(std::string_view instance_id,
    std::string_view account_id, std::string message) {
    auto& entry = slot_in(catalogues_, {instance_id, account_id});
    entry.state = catalogue_state::failed;
    entry.error = std::move(message);
}

const market_catalogue* market_table::catalogue(std::string_view instance_id,
    std::string_view account_id) const {
    return find_in(catalogues_, {instance_id, account_id});
}

namespace {

/**
 * @brief One level's buckets, taken from a backfill and kept newest first.
 *
 * A backfill is read as individual observations. The rings hold the last value
 * of each interval, so the observations are collapsed by bucket and the last
 * one in a bucket wins. The bucket carries the time of the observation that
 * won it, not the interval's start, so a plot shows the market's own time.
 */
[[nodiscard]] std::deque<domain::market_point> bucketize(
    const std::vector<domain::market_point>& points,
    domain::market_level level) {
    const auto interval = domain::market_level_ns(level);
    const auto capacity = domain::market_level_capacity(level);
    std::map<std::int64_t, domain::market_point> by_bucket;
    for (const auto& point : points) {
        if (point.at_ns <= 0)
            continue;
        auto [found, inserted] = by_bucket.try_emplace(point.at_ns / interval,
            domain::market_point{.at_ns = point.at_ns, .value = point.value,
                .open = point.value, .high = point.value,
                .low = point.value});
        if (inserted)
            continue;
        auto& bucket = found->second;
        bucket.at_ns = point.at_ns;
        bucket.value = point.value;
        bucket.high = std::max(bucket.high, point.value);
        bucket.low = std::min(bucket.low, point.value);
    }
    std::deque<domain::market_point> result;
    for (const auto& [bucket, point] : by_bucket) {
        static_cast<void>(bucket);
        result.push_back(point);
    }
    while (result.size() > capacity)
        result.pop_front();
    return result;
}

/**
 * @brief Merges a seeded level with what the subscription already delivered.
 *
 * The live points win on a shared timestamp. A backfill fetched while ticks
 * were arriving must not replace the newer prices with the ones the read
 * happened to see.
 */
void merge_level(std::deque<domain::market_point>& ring,
    std::deque<domain::market_point> seeded,
    std::size_t capacity) {
    if (seeded.empty())
        return;
    std::map<std::int64_t, domain::market_point> merged;
    for (const auto& point : seeded)
        merged[point.at_ns] = point;
    for (const auto& point : ring)
        merged[point.at_ns] = point;
    ring.clear();
    for (const auto& [at_ns, point] : merged) {
        static_cast<void>(at_ns);
        ring.push_back(point);
    }
    while (ring.size() > capacity)
        ring.pop_front();
}

void append_level(std::deque<domain::market_point>& ring,
    domain::market_point point, std::size_t capacity) {
    if (!ring.empty()) {
        if (point.at_ns < ring.back().at_ns)
            return;
        if (point.at_ns == ring.back().at_ns) {
            ring.back() = point;
            return;
        }
    }
    ring.push_back(point);
    while (ring.size() > capacity)
        ring.pop_front();
}

}

void market_table::set_points(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id,
    std::vector<domain::market_point> points, std::int64_t observed_at_ns) {
    auto& snapshot = slot_in(points_, {instance_id, account_id, series_id});
    merge_level(snapshot.fine, bucketize(points, domain::market_level::fine),
        domain::market_fine_capacity);
    merge_level(snapshot.medium,
        bucketize(points, domain::market_level::medium),
        domain::market_medium_capacity);
    merge_level(snapshot.coarse,
        bucketize(points, domain::market_level::coarse),
        domain::market_coarse_capacity);
    snapshot.observed_at_ns = std::max(snapshot.observed_at_ns, observed_at_ns);
    snapshot.failure.clear();
    snapshot.version = ++version_;
}

void market_table::append_point(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id,
    domain::market_level level, domain::market_point point,
    std::int64_t observed_at_ns) {
    auto& snapshot = slot_in(points_, {instance_id, account_id, series_id});
    switch (level) {
    case domain::market_level::fine:
        append_level(snapshot.fine, point, domain::market_fine_capacity);
        break;
    case domain::market_level::medium:
        append_level(snapshot.medium, point, domain::market_medium_capacity);
        break;
    case domain::market_level::coarse:
        append_level(snapshot.coarse, point, domain::market_coarse_capacity);
        break;
    }
    snapshot.observed_at_ns = std::max(snapshot.observed_at_ns, observed_at_ns);
    snapshot.failure.clear();
    snapshot.version = ++version_;
}

void market_table::fail_points(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id,
    std::string message, std::int64_t observed_at_ns) {
    auto& snapshot = slot_in(points_, {instance_id, account_id, series_id});
    snapshot.observed_at_ns = observed_at_ns;
    snapshot.failure = std::move(message);
    snapshot.version = ++version_;
}

const domain::market_series_snapshot* market_table::points(
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id) const {
    return find_in(points_, {instance_id, account_id, series_id});
}

void market_table::clear() {
    catalogues_.clear();
    points_.clear();
}

}
