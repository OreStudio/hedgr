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
#include "hedgr.model/state/image_cache.hpp"
#include <utility>

namespace hedgr::model::state {

namespace {
constexpr char separator = '\x1f';
}

image_cache::image_cache(std::size_t max_bytes)
    : max_bytes_(max_bytes) {
}

std::string image_cache::key_of(std::string_view tenant_id,
    std::string_view image_id) {
    std::string key;
    key.reserve(tenant_id.size() + image_id.size() + 1);
    key.append(tenant_id);
    key.push_back(separator);
    key.append(image_id);
    return key;
}

const image_content* image_cache::get(std::string_view tenant_id,
    std::string_view image_id) {
    auto& by_key = entries_.get<0>();
    const auto found = by_key.find(key_of(tenant_id, image_id));
    if (found == by_key.end()) {
        ++misses_;
        return nullptr;
    }
    auto& by_recency = entries_.get<1>();
    by_recency.relocate(by_recency.begin(), entries_.project<1>(found));
    ++hits_;
    return &found->image;
}

bool image_cache::contains(std::string_view tenant_id,
    std::string_view image_id) const {
    return entries_.get<0>().contains(key_of(tenant_id, image_id));
}

void image_cache::put(std::string_view tenant_id, std::string_view image_id,
    image_content image) {
    if (image.data.size() > max_bytes_)
        return;

    auto& by_key = entries_.get<0>();
    const auto key = key_of(tenant_id, image_id);
    if (const auto found = by_key.find(key); found != by_key.end()) {
        bytes_ -= found->image.data.size();
        by_key.erase(found);
    }

    auto& by_recency = entries_.get<1>();
    by_recency.push_front(entry{.key = key, .image = std::move(image)});
    bytes_ += by_recency.front().image.data.size();

    while (bytes_ > max_bytes_ && !by_recency.empty()) {
        bytes_ -= by_recency.back().image.data.size();
        by_recency.pop_back();
        ++evictions_;
    }
}

void image_cache::set_max_bytes(std::size_t max_bytes) {
    max_bytes_ = max_bytes;
    auto& by_recency = entries_.get<1>();
    while (bytes_ > max_bytes_ && !by_recency.empty()) {
        bytes_ -= by_recency.back().image.data.size();
        by_recency.pop_back();
        ++evictions_;
    }
}

void image_cache::clear() {
    entries_.clear();
    bytes_ = 0;
}

}
