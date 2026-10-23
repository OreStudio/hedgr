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
#include "hedgr.imgui/app/gl_texture.hpp"
#include "hedgr.ui/image/svg_raster.hpp"
#include <format>
#include <stb_image.h>

namespace hedgr::imgui::app {

image_texture_cache::image_texture_cache(std::size_t max_bytes)
    : max_bytes_(max_bytes) {
}

image_texture_cache::~image_texture_cache() {
    clear();
}

bool image_texture_cache::ensure(std::string_view tenant_id,
    std::string_view image_id, std::string_view mime_type,
    const std::vector<std::uint8_t>& data) {
    const auto key = model::state::make_tenant_key(tenant_id, image_id);
    if (failed_.contains(key))
        return false;
    auto& by_key = entries_.get<0>();
    if (by_key.contains(key))
        return true;

    unsigned int texture = 0;
    std::size_t bytes = 0;
    if (mime_type.find("svg") != std::string_view::npos) {
        const auto svg = std::string_view{
            reinterpret_cast<const char*>(data.data()), data.size()};
        if (const auto raster = ui::image::rasterize_svg(svg, 64, 44)) {
            texture = upload_rgba(raster->pixels.data(), raster->width,
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
            texture = upload_rgba(pixels, width, height);
            bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
            stbi_image_free(pixels);
        }
    }
    if (texture == 0) {
        failed_.insert(key);
        return false;
    }

    insert(key, tenant_id, image_id, texture, bytes);
    return true;
}

void image_texture_cache::insert(std::string key, std::string_view tenant_id,
    std::string_view image_id, unsigned int texture, std::size_t bytes) {
    entries_.get<1>().push_front(entry{.key = std::move(key),
        .tenant_id = std::string{tenant_id},
        .image_id = std::string{image_id},
        .texture = texture,
        .bytes = bytes,
        .used_frame = 0});
    bytes_ += bytes;
}

ImTextureID image_texture_cache::touch(
    container::nth_index<0>::type::iterator found) {
    auto& by_recency = entries_.get<1>();
    const auto position = entries_.project<1>(found);
    by_recency.relocate(by_recency.begin(), position);
    by_recency.modify(position, [this](entry& value) {
        value.used_frame = frame_;
    });
    return static_cast<ImTextureID>(found->texture);
}

ImTextureID image_texture_cache::get(std::string_view tenant_id,
    std::string_view image_id, std::string_view mime_type,
    const std::vector<std::uint8_t>& data) {
    auto& by_key = entries_.get<0>();
    if (!by_key.contains(model::state::make_tenant_key(tenant_id, image_id))
        && !ensure(tenant_id, image_id, mime_type, data))
        return 0;

    const auto found = by_key.find(
        model::state::make_tenant_key(tenant_id, image_id));
    if (found == by_key.end())
        return 0;
    return touch(found);
}

ImTextureID image_texture_cache::svg(std::string_view image_id,
    std::string_view document, int width, int height) {
    const auto key = model::state::make_tenant_key(
        "", std::format("{}@{}x{}", image_id, width, height));
    auto& by_key = entries_.get<0>();
    if (const auto found = by_key.find(key); found != by_key.end())
        return touch(found);
    if (failed_.contains(key))
        return 0;

    const auto raster = ui::image::rasterize_svg(document, width, height);
    if (!raster) {
        failed_.insert(key);
        return 0;
    }
    const unsigned int texture = upload_rgba(raster->pixels.data(),
        raster->width, raster->height);
    if (texture == 0) {
        failed_.insert(key);
        return 0;
    }
    insert(key, "", image_id, texture, raster->pixels.size());
    return touch(by_key.find(key));
}

ImTextureID image_texture_cache::svg_glow(std::string_view image_id,
    std::string_view document, int size, int inner, float sigma) {
    const auto key = model::state::make_tenant_key("",
        std::format("{}@glow{}/{}x{:.2f}", image_id, size, inner, sigma));
    auto& by_key = entries_.get<0>();
    if (const auto found = by_key.find(key); found != by_key.end())
        return touch(found);
    if (failed_.contains(key))
        return 0;

    const auto raster = ui::image::rasterize_svg_glow(document, size, inner,
        sigma);
    const unsigned int texture = raster
        ? upload_rgba(raster->pixels.data(), raster->width, raster->height) : 0;
    if (texture == 0) {
        failed_.insert(key);
        return 0;
    }
    insert(key, "", image_id, texture, raster->pixels.size());
    return touch(by_key.find(key));
}

void image_texture_cache::begin_frame() {
    ++frame_;
}

void image_texture_cache::end_frame() {
    auto& by_recency = entries_.get<1>();
    while (bytes_ > max_bytes_ && !by_recency.empty()) {
        auto victim = by_recency.end();
        for (auto it = by_recency.end(); it != by_recency.begin();) {
            --it;
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

}
