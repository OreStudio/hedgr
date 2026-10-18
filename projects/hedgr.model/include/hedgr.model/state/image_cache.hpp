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
#ifndef HEDGR_MODEL_STATE_IMAGE_CACHE_HPP
#define HEDGR_MODEL_STATE_IMAGE_CACHE_HPP

#include "hedgr.model/export.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::model::state {

/** @brief One tenant image as the assets service sent it. */
struct image_content final {
    std::string mime_type;
    std::vector<std::uint8_t> data;
};

/**
 * @brief Every tenant image Hedgr has read, bounded by bytes and evicted LRU.
 *
 * The key is the tenant and the image identifier, because an image belongs to
 * one tenant and the same identifier is not a global name. An image's bytes
 * never change for an identifier, so an entry is never stale and the cache has
 * no clock. Flags, avatars, party logos and staff photos all live here; a
 * consumer asks for the content it needs, and nothing about a particular
 * consumer is in the cache.
 *
 * The cache is single writer and is meant to be touched from the frame loop
 * only. Fetches happen on workers and arrive as events. A hit is one hash
 * lookup and one splice of the LRU list; eviction takes the least recently
 * used entry and drops it, which is what keeps a screen of staff photos from
 * growing without bound.
 */
class HEDGR_MODEL_EXPORT image_cache final {
public:
    /** @brief The default budget, enough for a few hundred photos and flags. */
    static constexpr std::size_t default_max_bytes = 32u * 1024u * 1024u;

    explicit image_cache(std::size_t max_bytes = default_max_bytes);

    /** @brief The image, touching its recency, or nullptr. */
    [[nodiscard]] const image_content*
    get(std::string_view tenant_id, std::string_view image_id);

    /** @brief Whether the image is held, without touching its recency. */
    [[nodiscard]] bool contains(std::string_view tenant_id,
        std::string_view image_id) const;

    /**
     * @brief Stores the image, replacing any previous entry and evicting LRU.
     * @return false when the image is larger than the whole budget.
     */
    [[nodiscard]] bool put(std::string_view tenant_id,
        std::string_view image_id, image_content image);

    /**
     * @brief Visits every cached image without copying or allocating.
     *
     * The texture cache walks this once a frame to upload what it does not yet
     * hold. A callback rather than a vector keeps the frame path free of a
     * copy of every image.
     */
    template<class Visitor>
    void for_each(Visitor&& visitor) const {
        for (const auto& entry : entries_)
            visitor(entry.tenant_id, entry.image_id, entry.image);
    }

    void clear();

    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t max_bytes() const noexcept { return max_bytes_; }
    /** @brief Changes the budget and evicts until it is met. */
    void set_max_bytes(std::size_t max_bytes);

    [[nodiscard]] std::uint64_t hits() const noexcept { return hits_; }
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_; }
    [[nodiscard]] std::uint64_t evictions() const noexcept {
        return evictions_;
    }

private:
    /**
     * @brief One entry, indexed by key and ordered by recency.
     *
     * Boost.MultiIndex, from the Boost that vcpkg already ships, gives the
     * hash lookup and the recency order over one set of entries. A hit is a
     * hashed lookup and one relocation to the front; an eviction is one pop
     * from the back. No cache primitive exists in ORE Studio and the only
     * vcpkg cache engine is CacheLib, a server cache, which is far more than
     * a client of a few hundred images needs.
     */
    struct entry final {
        std::string key;
        std::string tenant_id;
        std::string image_id;
        image_content image;
    };

    using container = boost::multi_index_container<entry,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::member<
                entry, std::string, &entry::key>>,
            boost::multi_index::sequenced<>>>;

    container entries_;
    std::size_t bytes_ = 0;
    std::size_t max_bytes_ = default_max_bytes;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
};

}

#endif
