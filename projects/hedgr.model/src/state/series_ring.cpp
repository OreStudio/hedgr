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
#include "hedgr.model/state/series_ring.hpp"
#include <algorithm>

namespace hedgr::model::state {

series_ring::series_ring(std::size_t capacity)
    : capacity_(capacity),
      total_(capacity + std::max<std::size_t>(capacity / 4, 1)) {}

void series_ring::write(std::size_t index, const series_point& point) noexcept {
    at_s_[index] = static_cast<double>(point.at_ns) / 1e9;
    value_[index] = point.value;
    at_ns_[index] = point.at_ns;
    open_[index] = point.open;
    high_[index] = point.high;
    low_[index] = point.low;
}

void series_ring::grow_or_compact() {
    const auto allocated = at_ns_.size();
    if (allocated < total_) {
        const auto next = std::min(
            std::max<std::size_t>(allocated * 2, 16), total_);
        at_s_.resize(next);
        value_.resize(next);
        at_ns_.resize(next);
        open_.resize(next);
        high_.resize(next);
        low_.resize(next);
        return;
    }
    const auto first = static_cast<std::ptrdiff_t>(begin_);
    const auto last = static_cast<std::ptrdiff_t>(end_);
    std::copy(at_s_.begin() + first, at_s_.begin() + last, at_s_.begin());
    std::copy(value_.begin() + first, value_.begin() + last, value_.begin());
    std::copy(at_ns_.begin() + first, at_ns_.begin() + last, at_ns_.begin());
    std::copy(open_.begin() + first, open_.begin() + last, open_.begin());
    std::copy(high_.begin() + first, high_.begin() + last, high_.begin());
    std::copy(low_.begin() + first, low_.begin() + last, low_.begin());
    end_ -= begin_;
    begin_ = 0;
}

append_outcome series_ring::append(const series_point& point) {
    if (size() != 0) {
        const auto tail = at_ns_[end_ - 1];
        if (point.at_ns < tail)
            return append_outcome::late;
        if (point.at_ns == tail) {
            write(end_ - 1, point);
            return append_outcome::replaced;
        }
    }
    if (end_ == at_ns_.size())
        grow_or_compact();
    write(end_, point);
    ++end_;
    if (size() > capacity_) {
        ++begin_;
        ++first_seq_;
    }
    return append_outcome::appended;
}

bool series_ring::merge(std::span<const series_point> seeded,
    std::int64_t bucket_ns) {
    if (seeded.empty())
        return false;
    std::vector<series_point> merged;
    merged.reserve(size() + seeded.size());
    std::size_t s = 0;
    std::size_t h = begin_;
    while (s < seeded.size() || h < end_) {
        if (h == end_) {
            merged.push_back(seeded[s++]);
            continue;
        }
        const series_point held{.at_ns = at_ns_[h], .value = value_[h],
            .open = open_[h], .high = high_[h], .low = low_[h]};
        if (s == seeded.size()) {
            merged.push_back(held);
            ++h;
            continue;
        }
        const auto seeded_bucket = seeded[s].at_ns / bucket_ns;
        const auto held_bucket = held.at_ns / bucket_ns;
        if (seeded_bucket < held_bucket) {
            merged.push_back(seeded[s++]);
        } else {
            merged.push_back(held);
            ++h;
            if (seeded_bucket == held_bucket)
                ++s;
        }
    }
    if (merged.size() > capacity_)
        merged.erase(merged.begin(), merged.end() -
            static_cast<std::ptrdiff_t>(capacity_));

    bool same = merged.size() == size();
    for (std::size_t i = 0; same && i < merged.size(); ++i) {
        const auto at = begin_ + i;
        same = merged[i] == series_point{.at_ns = at_ns_[at],
            .value = value_[at], .open = open_[at], .high = high_[at],
            .low = low_[at]};
    }
    if (same)
        return false;

    first_seq_ += size();
    while (at_ns_.size() < merged.size() + 1)
        grow_or_compact();
    begin_ = 0;
    end_ = merged.size();
    for (std::size_t i = 0; i < merged.size(); ++i)
        write(i, merged[i]);
    return true;
}

series_columns series_ring::columns() const noexcept {
    series_columns cols;
    cols.first_seq = first_seq_;
    if (size() == 0)
        return cols;
    cols.at_s = at_s_.data() + begin_;
    cols.value = value_.data() + begin_;
    cols.at_ns = at_ns_.data() + begin_;
    cols.open = open_.data() + begin_;
    cols.high = high_.data() + begin_;
    cols.low = low_.data() + begin_;
    cols.size = size();
    return cols;
}

std::int64_t series_ring::newest_at_ns() const noexcept {
    return size() == 0 ? 0 : at_ns_[end_ - 1];
}

std::size_t series_ring::bytes() const noexcept {
    return at_ns_.size() * 6 * sizeof(double);
}

}
