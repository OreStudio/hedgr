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
#include "hedgr.model/state/series_table.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace hedgr::model::state {

const domain::market_series_row*
series_catalogue::row_of(std::string_view series_id) const {
    const auto found = index.find(series_id);
    return found == index.end() ? nullptr : &series[found->second];
}

namespace {

[[nodiscard]] std::size_t level_index(domain::market_level level) noexcept {
    return static_cast<std::size_t>(level);
}

/**
 * @brief One level's buckets from a backfill, oldest first.
 *
 * A backfill is read as individual observations. A ring holds the last value of
 * each interval, so the observations are collapsed by bucket and the last one
 * in a bucket wins. The bucket carries the time of the observation that won it,
 * so a plot shows the market's own time.
 */
[[nodiscard]] std::vector<series_point> bucketize(
    std::span<const domain::market_point> points, domain::market_level level) {
    const auto interval = domain::market_level_ns(level);
    const auto capacity = domain::market_level_capacity(level);
    std::map<std::int64_t, series_point> by_bucket;
    for (const auto& point : points) {
        if (point.at_ns <= 0)
            continue;
        const auto [found, inserted] = by_bucket.try_emplace(
            point.at_ns / interval,
            series_point{.at_ns = point.at_ns, .value = point.value,
                .open = point.value, .high = point.value, .low = point.value});
        if (inserted)
            continue;
        auto& bucket = found->second;
        bucket.at_ns = point.at_ns;
        bucket.value = point.value;
        bucket.high = std::max(bucket.high, point.value);
        bucket.low = std::min(bucket.low, point.value);
    }
    std::vector<series_point> result;
    result.reserve(by_bucket.size());
    for (const auto& entry : by_bucket)
        result.push_back(entry.second);
    if (result.size() > capacity)
        result.erase(result.begin(), result.end() -
            static_cast<std::ptrdiff_t>(capacity));
    return result;
}

}

series_table::series_table(std::size_t budget_bytes) : budget_(budget_bytes) {}

series_table::row* series_table::row_of(series_handle h) noexcept {
    return owns(layout_, h) ? &rows_[h.index] : nullptr;
}

const series_table::row*
series_table::row_of(series_handle h) const noexcept {
    return owns(layout_, h) ? &rows_[h.index] : nullptr;
}

std::size_t series_table::ring_bytes(const row& r) const noexcept {
    std::size_t total = 0;
    for (const auto& ring : r.rings)
        total += ring.bytes();
    return total;
}

void series_table::refresh(row& r, const series_catalogue& catalogue) {
    if (const auto* listed = catalogue.row_of(r.series_id)) {
        r.info.uri = listed->uri;
        r.info.party_id = listed->party_id;
        r.info.tenor = listed->tenor;
        r.info.curve_key = listed->curve_key;
        r.info.listed = true;
    } else {
        r.info.listed = false;
    }
}

void series_table::apply_catalogue(std::string_view instance_id,
    std::string_view account_id, std::vector<domain::market_series_row> rows) {
    auto& entry = slot_in(scopes_, {instance_id, account_id});
    auto& catalogue = entry.catalogue;
    catalogue.series = std::move(rows);
    catalogue.index.clear();
    catalogue.index.reserve(catalogue.series.size());
    for (std::size_t i = 0; i < catalogue.series.size(); ++i)
        catalogue.index.try_emplace(catalogue.series[i].id, i);
    catalogue.state = catalogue_state::ready;
    catalogue.error.clear();
    catalogue.generation = ++counter_;
    for (const auto held : entry.held)
        refresh(rows_[held.index], catalogue);
}

void series_table::begin_catalogue(std::string_view instance_id,
    std::string_view account_id) {
    auto& catalogue = slot_in(scopes_, {instance_id, account_id}).catalogue;
    catalogue.state = catalogue_state::reading;
    catalogue.error.clear();
    catalogue.generation = ++counter_;
}

void series_table::fail_catalogue(std::string_view instance_id,
    std::string_view account_id, std::string message) {
    auto& catalogue = slot_in(scopes_, {instance_id, account_id}).catalogue;
    catalogue.state = catalogue_state::failed;
    catalogue.error = std::move(message);
    catalogue.generation = ++counter_;
}

const series_catalogue* series_table::catalogue(std::string_view instance_id,
    std::string_view account_id) const {
    const auto* entry = find_in(scopes_, {instance_id, account_id});
    return entry != nullptr && entry->catalogue.generation != 0
        ? &entry->catalogue : nullptr;
}

series_handle series_table::intern(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    if (const auto* held = find_in(keys_, {instance_id, account_id, series_id}))
        return *held;
    const auto index = layout_.allocate();
    if (index == rows_.size())
        rows_.emplace_back();
    else
        rows_[index] = row{};
    auto& r = rows_[index];
    r.instance_id = instance_id;
    r.account_id = account_id;
    r.series_id = series_id;
    r.released_at = ++counter_;
    const series_handle h{index, layout_.generation[index]};
    keys_.try_emplace(
        composite_key<3>{{instance_id, account_id, series_id}}, h);
    auto& entry = slot_in(scopes_, {instance_id, account_id});
    entry.held.push_back(h);
    if (entry.catalogue.generation != 0)
        refresh(r, entry.catalogue);
    return h;
}

std::optional<series_handle> series_table::find(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) const {
    if (const auto* held = find_in(keys_, {instance_id, account_id, series_id}))
        return *held;
    return std::nullopt;
}

bool series_table::contains(series_handle h) const noexcept {
    return owns(layout_, h);
}

void series_table::acquire(series_handle h) noexcept {
    if (auto* r = row_of(h))
        ++r->watchers;
}

void series_table::release(series_handle h) {
    auto* r = row_of(h);
    if (r == nullptr || r->watchers == 0)
        return;
    if (--r->watchers != 0)
        return;
    r->released_at = ++counter_;
    evict_to_budget();
}

void series_table::erase(series_handle h) {
    auto* r = row_of(h);
    if (r == nullptr)
        return;
    resident_ -= ring_bytes(*r);
    const key_parts<3> key{r->instance_id, r->account_id, r->series_id};
    const key_parts<2> scope_key{r->instance_id, r->account_id};
    keys_.erase(keys_.find(key));
    const auto entry = scopes_.find(scope_key);
    auto& held = entry->second.held;
    held.erase(std::find(held.begin(), held.end(), h));
    if (held.empty() && entry->second.catalogue.generation == 0)
        scopes_.erase(entry);
    rows_[h.index] = row{};
    layout_.release(h.index);
}

series_columns series_table::columns(series_handle h,
    domain::market_level level) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? series_columns{} : r->rings[level_index(level)].columns();
}

append_result series_table::append(series_handle h, domain::market_level level,
    const series_point& point, std::int64_t observed_at_ns) {
    auto* r = row_of(h);
    if (r == nullptr)
        return append_result::stale;
    auto& ring = r->rings[level_index(level)];
    const auto before = ring.bytes();
    const auto outcome = ring.append(point);
    resident_ += ring.bytes() - before;
    r->observed_at_ns = std::max(r->observed_at_ns, observed_at_ns);
    const auto had_failure = !r->failure.empty();
    r->failure.clear();
    if (outcome == append_outcome::late) {
        if (had_failure)
            r->stamp = ++counter_;
        return append_result::late;
    }
    r->stamp = ++counter_;
    return append_result::applied;
}

bool series_table::seed(series_handle h,
    std::span<const domain::market_point> points,
    std::int64_t observed_at_ns) {
    auto* r = row_of(h);
    if (r == nullptr)
        return false;
    bool changed = false;
    for (const auto level : {domain::market_level::fine,
             domain::market_level::medium, domain::market_level::coarse}) {
        const auto buckets = bucketize(points, level);
        auto& ring = r->rings[level_index(level)];
        const auto before = ring.bytes();
        changed |= ring.merge(buckets, domain::market_level_ns(level));
        resident_ += ring.bytes() - before;
    }
    if (observed_at_ns > r->observed_at_ns) {
        r->observed_at_ns = observed_at_ns;
        changed = true;
    }
    if (!r->failure.empty()) {
        r->failure.clear();
        changed = true;
    }
    if (changed)
        r->stamp = ++counter_;
    return changed;
}

void series_table::fail(series_handle h, std::string message,
    std::int64_t observed_at_ns) {
    auto* r = row_of(h);
    if (r == nullptr)
        return;
    r->observed_at_ns = observed_at_ns;
    r->failure = std::move(message);
    r->stamp = ++counter_;
}

std::uint64_t series_table::version(series_handle h) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? 0 : r->stamp;
}

std::string_view series_table::failure(series_handle h) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? std::string_view{} : std::string_view{r->failure};
}

std::int64_t series_table::observed_at_ns(series_handle h) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? 0 : r->observed_at_ns;
}

std::int64_t series_table::newest_at_ns(series_handle h) const noexcept {
    const auto* r = row_of(h);
    if (r == nullptr)
        return 0;
    std::int64_t newest = 0;
    for (const auto& ring : r->rings)
        newest = std::max(newest, ring.newest_at_ns());
    return newest;
}

const series_info* series_table::info(series_handle h) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? nullptr : &r->info;
}

std::uint32_t series_table::watchers(series_handle h) const noexcept {
    const auto* r = row_of(h);
    return r == nullptr ? 0 : r->watchers;
}

std::size_t series_table::forget_scope(std::string_view instance_id,
    std::string_view account_id) {
    const auto entry = scopes_.find(key_parts<2>{instance_id, account_id});
    if (entry == scopes_.end())
        return 0;
    entry->second.catalogue = series_catalogue{};
    const auto held = entry->second.held;
    for (const auto h : held)
        erase(h);
    if (held.empty())
        scopes_.erase(entry);
    return held.size();
}

void series_table::clear() {
    for (std::uint32_t i = 0; i < layout_.capacity(); ++i) {
        if (layout_.is_alive(i))
            erase({i, layout_.generation[i]});
    }
    scopes_.clear();
}

void series_table::set_budget(std::size_t budget_bytes) {
    budget_ = budget_bytes;
    evict_to_budget();
}

void series_table::evict_to_budget() {
    while (resident_ > budget_) {
        std::optional<series_handle> oldest;
        std::uint64_t oldest_at = std::numeric_limits<std::uint64_t>::max();
        for (std::uint32_t i = 0; i < layout_.capacity(); ++i) {
            if (!layout_.is_alive(i) || rows_[i].watchers != 0)
                continue;
            if (rows_[i].released_at < oldest_at) {
                oldest_at = rows_[i].released_at;
                oldest = series_handle{i, layout_.generation[i]};
            }
        }
        if (!oldest)
            return;
        erase(*oldest);
    }
}

}
