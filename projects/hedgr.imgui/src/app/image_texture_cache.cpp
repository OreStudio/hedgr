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
#include "hedgr.imgui/app/image_texture_cache.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include "hedgr.ui/image/svg_raster.hpp"
#include <GL/glew.h>
#include <stb_image.h>

namespace hedgr::imgui::app {

image_texture_cache::image_texture_cache(std::size_t max_bytes)
    : max_bytes_(max_bytes) {
}

image_texture_cache::~image_texture_cache() {
    clear();
}

void image_texture_cache::ensure(std::string_view tenant_id,
    std::string_view image_id, std::string_view mime_type,
    std::vector<std::uint8_t> const& data) {
    const auto key = model::state::make_tenant_key(tenant_id, image_id);
    if (failed_.contains(key))
        return;
    auto& by_key = entries_.get<0>();
    if (by_key.contains(key))
        return;

    unsigned int texture = 0;
    std::size_t bytes = 0;
    if (mime_type.find("svg") != std::string_view::npos) {
        const auto svg = std::string_view{
            reinterpret_cast<const char*>(data.data()), data.size()};
        if (const auto raster = ui::image::rasterize_svg(svg, 64, 44)) {
            texture = upload(raster->pixels.data(), raster->width,
                raster->height);
            bytes = raster->pixels.size();
        }
    } else {
        int width = 0;
        int height = 0;
        int channels = 0;
        auto* pixels = stbi_load_from_memory(data.data(),
            static_cast<int>(data.size()), &width, &height, &channels, 4);
        if (pixels != nullptr) {
            texture = upload(pixels, width, height);
            bytes = static_cast<std::size_t>(width) * height * 4;
            stbi_image_free(pixels);
        }
    }
    if (texture == 0) {
        failed_.insert(key);
        return;
    }

    auto& by_recency = entries_.get<1>();
    by_recency.push_front(entry{.key = key,
        .tenant_id = std::string{tenant_id},
        .image_id = std::string{image_id},
        .texture = texture,
        .bytes = bytes,
        .used_frame = 0});
    bytes_ += bytes;
}

ImTextureID image_texture_cache::get(std::string_view tenant_id,
    std::string_view image_id) {
    auto& by_key = entries_.get<0>();
    const auto found = by_key.find(
        model::state::make_tenant_key(tenant_id, image_id));
    if (found == by_key.end())
        return 0;
    auto& by_recency = entries_.get<1>();
    const auto position = entries_.project<1>(found);
    by_recency.relocate(by_recency.begin(), position);
    by_recency.modify(position, [this](entry& value) {
        value.used_frame = frame_;
    });
    return static_cast<ImTextureID>(found->texture);
}

void image_texture_cache::begin_frame() {
    ++frame_;
}

void image_texture_cache::end_frame() {
    auto& by_recency = entries_.get<1>();
    while (bytes_ > max_bytes_ && !by_recency.empty()) {
        auto victim = by_recency.end();
        for (auto it = by_recency.begin(); it != by_recency.end(); ++it) {
            if (it->used_frame != frame_) {
                victim = it;
                break;
            }
        }
        if (victim == by_recency.end())
            break;
        bytes_ -= victim->bytes;
        glDeleteTextures(1, &victim->texture);
        ++evictions_;
        by_recency.erase(victim);
    }
}

void image_texture_cache::clear() {
    auto& by_key = entries_.get<0>();
    for (auto& item : by_key)
        glDeleteTextures(1, &item.texture);
    entries_.clear();
    failed_.clear();
    bytes_ = 0;
}

unsigned int image_texture_cache::upload(const unsigned char* pixels,
    int width, int height) {
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, pixels);
    return texture;
}

}
