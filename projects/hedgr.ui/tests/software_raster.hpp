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
#ifndef HEDGR_UI_TESTS_SOFTWARE_RASTER_HPP
#define HEDGR_UI_TESTS_SOFTWARE_RASTER_HPP

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace hedgr::testing {

/**
 * @brief Paints ImGui draw data into memory, so a headless frame can be seen.
 *
 * A reference rasteriser rather than a renderer: textured, vertex coloured
 * triangles, bilinear sampling, scissor clipping and straight alpha blending,
 * which is what every ImGui backend asks of the GPU. It is slow and exists only
 * so that a test can write a frame to a file.
 */
class software_raster final {
public:
    software_raster(int width, int height)
        : width_(width), height_(height),
          pixels_(static_cast<std::size_t>(width * height) * 3, 0.0f) {}

    /** @brief Takes the atlas pages ImGui asks a backend to create or update. */
    void update_textures() {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
            if (texture->Status == ImTextureStatus_WantDestroy) {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
                continue;
            }
            if (texture->Status != ImTextureStatus_WantCreate
                && texture->Status != ImTextureStatus_WantUpdates)
                continue;
            page copy{texture->Width, texture->Height, {}};
            const auto count = static_cast<std::size_t>(copy.width * copy.height);
            copy.rgba.resize(count * 4);
            const auto* source =
                static_cast<const std::uint8_t*>(texture->GetPixels());
            if (texture->Format == ImTextureFormat_RGBA32) {
                std::memcpy(copy.rgba.data(), source, count * 4);
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    copy.rgba[i * 4 + 0] = 255;
                    copy.rgba[i * 4 + 1] = 255;
                    copy.rgba[i * 4 + 2] = 255;
                    copy.rgba[i * 4 + 3] = source[i];
                }
            }
            if (texture->TexID == ImTextureID_Invalid) {
                pages_.push_back(std::move(copy));
                texture->SetTexID(static_cast<ImTextureID>(pages_.size()));
            } else {
                pages_[static_cast<std::size_t>(texture->TexID) - 1] =
                    std::move(copy);
            }
            texture->SetStatus(ImTextureStatus_OK);
        }
    }

    /** @brief Clears to @p colour and paints @p data over it. */
    void draw(const ImDrawData& data, ImU32 colour) {
        const float clear[3] = {channel(colour, IM_COL32_R_SHIFT),
            channel(colour, IM_COL32_G_SHIFT), channel(colour, IM_COL32_B_SHIFT)};
        for (std::size_t i = 0; i < pixels_.size(); ++i)
            pixels_[i] = clear[i % 3];
        for (const ImDrawList* list : data.CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback != nullptr || command.ElemCount == 0)
                    continue;
                const ImVec4 clip(command.ClipRect.x - data.DisplayPos.x,
                    command.ClipRect.y - data.DisplayPos.y,
                    command.ClipRect.z - data.DisplayPos.x,
                    command.ClipRect.w - data.DisplayPos.y);
                const page* texture = page_for(command.GetTexID());
                for (unsigned int i = 0; i < command.ElemCount; i += 3) {
                    const auto vertex = [&](unsigned int k) -> const ImDrawVert& {
                        return list->VtxBuffer[static_cast<int>(command.VtxOffset
                            + list->IdxBuffer[static_cast<int>(
                                command.IdxOffset + i + k)])];
                    };
                    triangle(vertex(0), vertex(1), vertex(2), clip, texture,
                        data.DisplayPos);
                }
            }
        }
    }

    /** @brief Writes the frame as a binary PPM. */
    bool write_ppm(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << width_ << ' ' << height_ << "\n255\n";
        for (const float value : pixels_) {
            const auto byte = static_cast<char>(static_cast<std::uint8_t>(
                std::clamp(std::lround(value * 255.0f), 0L, 255L)));
            file.put(byte);
        }
        return static_cast<bool>(file);
    }

private:
    struct page final {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;
    };

    static float channel(ImU32 colour, unsigned int shift) {
        return static_cast<float>((colour >> shift) & 0xFFu) / 255.0f;
    }

    const page* page_for(ImTextureID id) const {
        if (id == ImTextureID_Invalid || id > pages_.size())
            return nullptr;
        return &pages_[static_cast<std::size_t>(id) - 1];
    }

    static void sample(const page* texture, float u, float v, float out[4]) {
        if (texture == nullptr) {
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            return;
        }
        const float x = u * static_cast<float>(texture->width) - 0.5f;
        const float y = v * static_cast<float>(texture->height) - 0.5f;
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const auto texel = [&](int tx, int ty, int c) {
            tx = std::clamp(tx, 0, texture->width - 1);
            ty = std::clamp(ty, 0, texture->height - 1);
            return static_cast<float>(texture->rgba[static_cast<std::size_t>(
                (ty * texture->width + tx) * 4 + c)]) / 255.0f;
        };
        for (int c = 0; c < 4; ++c) {
            const float top = texel(x0, y0, c) * (1.0f - fx) + texel(x0 + 1, y0, c) * fx;
            const float bottom =
                texel(x0, y0 + 1, c) * (1.0f - fx) + texel(x0 + 1, y0 + 1, c) * fx;
            out[c] = top * (1.0f - fy) + bottom * fy;
        }
    }

    void triangle(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
        const ImVec4& clip, const page* texture, ImVec2 offset) {
        const ImVec2 pa(a.pos.x - offset.x, a.pos.y - offset.y);
        const ImVec2 pb(b.pos.x - offset.x, b.pos.y - offset.y);
        const ImVec2 pc(c.pos.x - offset.x, c.pos.y - offset.y);
        const float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
        if (std::fabs(area) < 1e-6f)
            return;
        const int left = std::max({0, static_cast<int>(std::floor(std::max(
            std::min({pa.x, pb.x, pc.x}), clip.x)))});
        const int top = std::max({0, static_cast<int>(std::floor(std::max(
            std::min({pa.y, pb.y, pc.y}), clip.y)))});
        const int right = std::min(width_, static_cast<int>(std::ceil(std::min(
            std::max({pa.x, pb.x, pc.x}), clip.z))));
        const int bottom = std::min(height_, static_cast<int>(std::ceil(std::min(
            std::max({pa.y, pb.y, pc.y}), clip.w))));
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                if (px < clip.x || px >= clip.z || py < clip.y || py >= clip.w)
                    continue;
                const float w0 = ((pb.x - px) * (pc.y - py) - (pb.y - py) * (pc.x - px)) / area;
                const float w1 = ((pc.x - px) * (pa.y - py) - (pc.y - py) * (pa.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                    continue;
                float texel[4];
                sample(texture, a.uv.x * w0 + b.uv.x * w1 + c.uv.x * w2,
                    a.uv.y * w0 + b.uv.y * w1 + c.uv.y * w2, texel);
                float source[4];
                const unsigned int shifts[4] = {IM_COL32_R_SHIFT,
                    IM_COL32_G_SHIFT, IM_COL32_B_SHIFT, IM_COL32_A_SHIFT};
                for (int k = 0; k < 4; ++k) {
                    source[k] = texel[k] * (channel(a.col, shifts[k]) * w0
                        + channel(b.col, shifts[k]) * w1
                        + channel(c.col, shifts[k]) * w2);
                }
                float* target = &pixels_[static_cast<std::size_t>((y * width_ + x) * 3)];
                for (int k = 0; k < 3; ++k)
                    target[k] = source[k] * source[3] + target[k] * (1.0f - source[3]);
            }
        }
    }

    int width_;
    int height_;
    std::vector<float> pixels_;
    std::vector<page> pages_;
};

}

#endif
