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
#ifndef HEDGR_IMGUI_APP_IMAGE_TEXTURE_CACHE_HPP
#define HEDGR_IMGUI_APP_IMAGE_TEXTURE_CACHE_HPP

#include "hedgr.ui/render/renderer.hpp"
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hedgr::imgui::app {

/**
 * @brief Uploaded tenant images, bounded by decoded bytes and evicted LRU.
 *
 * The interface library draws textures but owns no graphics context, so this
 * lives beside the window. It is keyed by tenant and image identifier, the
 * same identity as the byte cache. A texture used in the current frame is
 * pinned; at the end of the frame the least recently used unpinned textures
 * leave until the decoded budget is met again.
 */
class image_texture_cache final : public ui::render::image_texture_source {
public:
    static constexpr std::size_t default_max_bytes = 64u * 1024u * 1024u;

    explicit image_texture_cache(std::size_t max_bytes = default_max_bytes);
    ~image_texture_cache() override;

    image_texture_cache(const image_texture_cache&) = delete;
    image_texture_cache& operator=(const image_texture_cache&) = delete;

    [[nodiscard]] ImTextureID get(std::string_view tenant_id,
        std::string_view image_id, std::string_view mime_type,
        const std::vector<std::uint8_t>& data) override;

    [[nodiscard]] ImTextureID svg(std::string_view image_id,
        std::string_view document, int width, int height) override;

    /** @brief Starts a frame; textures used after this are pinned. */
    void begin_frame();

    /** @brief Evicts unpinned LRU textures until the budget is met. */
    void end_frame();

    /** @brief Deletes every texture; call with the context alive. */
    void clear();

    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t evictions() const noexcept {
        return evictions_;
    }

private:
    struct entry final {
        std::string key;
        std::string tenant_id;
        std::string image_id;
        unsigned int texture = 0;
        std::size_t bytes = 0;
        std::uint64_t used_frame = 0;
    };

    using container = boost::multi_index_container<entry,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::member<
                entry, std::string, &entry::key>>,
            boost::multi_index::sequenced<>>>;

    bool ensure(std::string_view tenant_id, std::string_view image_id,
        std::string_view mime_type,
        const std::vector<std::uint8_t>& data);

    /** @brief Stores an uploaded texture under @p key, most recent first. */
    void insert(std::string key, std::string_view tenant_id,
        std::string_view image_id, unsigned int texture, std::size_t bytes);

    /** @brief Marks @p found used by this frame and returns its texture. */
    ImTextureID touch(container::nth_index<0>::type::iterator found);

    container entries_;
    std::size_t bytes_ = 0;
    std::size_t max_bytes_ = default_max_bytes;
    std::size_t evictions_ = 0;
    std::uint64_t frame_ = 0;
    std::unordered_set<std::string> failed_;
};

}

#endif
