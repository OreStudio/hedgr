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
#include "hedgr.ui/render/intro.hpp"

#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

constexpr const char* logo_texture_id = "ore-logo-white";
constexpr int fan_segments = 32;

[[nodiscard]] ImU32 colour(float r, float g, float b, float a) {
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(r, g, b, std::clamp(a, 0.0f, 1.0f)));
}

/**
 * @brief A disc whose colour runs from @p inner at the centre to @p outer at
 * the rim.
 *
 * ImGui has no radial gradient, so this is a fan of triangles with the two
 * colours on its vertices. It reserves its vertices in one call, so it costs
 * no allocation once the list has grown to its steady size.
 */
void radial_fan(ImDrawList* list, ImVec2 centre, float radius, ImU32 inner,
    ImU32 outer) {
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    const ImDrawIdx base = static_cast<ImDrawIdx>(list->_VtxCurrentIdx);
    list->PrimReserve(fan_segments * 3, fan_segments + 1);
    list->PrimWriteVtx(centre, white, inner);
    constexpr float two_pi = 6.2831853f;
    for (int i = 0; i < fan_segments; ++i) {
        const float angle = two_pi * static_cast<float>(i)
            / static_cast<float>(fan_segments);
        list->PrimWriteVtx(ImVec2(centre.x + std::cos(angle) * radius,
            centre.y + std::sin(angle) * radius), white, outer);
    }
    for (int i = 0; i < fan_segments; ++i) {
        list->PrimWriteIdx(base);
        list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
        list->PrimWriteIdx(static_cast<ImDrawIdx>(
            base + 1 + (i + 1) % fan_segments));
    }
}

/** @brief The glow colour of layer @p index: the core, moving toward the purple. */
[[nodiscard]] ImVec4 layer_colour(int index) {
    namespace theme = hedgr::ui::theme;
    const ImVec4 purple = theme::chart_line_colour();
    const float mix = theme::intro_glow_purple_mix * static_cast<float>(index)
        / static_cast<float>(theme::intro_glow_layers - 1);
    return ImVec4(
        theme::intro_core_rgb[0] + (purple.x - theme::intro_core_rgb[0]) * mix,
        theme::intro_core_rgb[1] + (purple.y - theme::intro_core_rgb[1]) * mix,
        theme::intro_core_rgb[2] + (purple.z - theme::intro_core_rgb[2]) * mix,
        1.0f);
}

}

namespace hedgr::ui::render {

bool intro_skip_requested() {
    for (int mouse = 0; mouse < 3; ++mouse) {
        if (ImGui::IsMouseClicked(mouse, false))
            return true;
    }
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
        const auto named = static_cast<ImGuiKey>(key);
        if (named >= ImGuiKey_MouseLeft && named <= ImGuiKey_MouseWheelY)
            continue;
        if (ImGui::IsKeyPressed(named, false))
            return true;
    }
    return false;
}

void draw_intro(const render_input& input, std::string_view logo_svg,
    std::int64_t elapsed_ns) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 extent(origin.x + viewport->Size.x, origin.y + viewport->Size.y);
    const ImVec2 centre(origin.x + viewport->Size.x * 0.5f,
        origin.y + viewport->Size.y * 0.5f);
    const float veil = intro_timing::veil_alpha(elapsed_ns);
    const float glow = intro_timing::brightness(elapsed_ns);
    const float bloom = intro_timing::expansion(elapsed_ns);
    /* The glow layers thin as they spread; the crisp shapes do not. */
    const float layer_fade = 1.0f - theme::intro_glow_expand_fade * bloom;
    const float crisp_alpha = theme::intro_peak_intensity * glow * veil;

    /*
     * Black under the wallpaper, so a launch whose wallpaper failed to load is
     * still a dark scene, and so the fade at the end reveals the main window
     * through one surface rather than two.
     */
    list->AddRectFilled(origin, extent, colour(0.0f, 0.0f, 0.0f, veil));
    if (input.background_texture != 0) {
        const auto grey = static_cast<float>(theme::intro_wallpaper_grey) / 255.0f;
        list->AddImage(static_cast<ImTextureID>(input.background_texture),
            origin, extent, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            colour(grey, grey, grey, veil));
    }

    const float side = std::min(viewport->Size.x, viewport->Size.y)
        * theme::intro_logo_fraction;
    if (glow > 0.0f) {
        const float strength = glow * veil;
        const ImVec4 inner = layer_colour(theme::intro_glow_layers - 1);
        radial_fan(list, centre,
            side * theme::intro_halo_radius * intro_timing::spread(bloom),
            colour(inner.x, inner.y, inner.z,
                theme::intro_halo_alpha * strength),
            colour(inner.x, inner.y, inner.z, 0.0f));
    }
    radial_fan(list, centre,
        0.5f * std::hypot(viewport->Size.x, viewport->Size.y),
        colour(0.0f, 0.0f, 0.0f, 0.0f),
        colour(0.0f, 0.0f, 0.0f, theme::intro_vignette_alpha * veil));

    if (glow <= 0.0f)
        return;

    /*
     * The word is the same layers as the logo, each a larger size of the same
     * text centred on the same point, outermost first, then the crisp text.
     */
    ImFont* const font = icon::bold_font() != nullptr ? icon::bold_font()
                                                       : ImGui::GetFont();
    const float word_size = side * theme::intro_word_size;
    const ImVec2 word_centre(centre.x,
        centre.y + side * (0.5f + theme::intro_word_gap) + word_size * 0.5f);
    const auto draw_word = [&](float scale, ImU32 tint) {
        const float size = word_size * scale;
        const ImVec2 extent_px = font->CalcTextSizeA(size, FLT_MAX, 0.0f,
            theme::intro_word);
        list->AddText(font, size,
            ImVec2(word_centre.x - extent_px.x * 0.5f,
                word_centre.y - extent_px.y * 0.5f),
            tint, theme::intro_word);
    };
    for (int i = theme::intro_glow_layers - 1; i >= 0; --i) {
        const ImVec4 c = layer_colour(i);
        draw_word(intro_timing::glow_scale(theme::intro_glow_scales[i], bloom),
            colour(c.x, c.y, c.z,
                theme::intro_glow_alphas[i] * glow * veil * layer_fade));
    }
    draw_word(1.0f, colour(theme::intro_core_rgb[0], theme::intro_core_rgb[1],
        theme::intro_core_rgb[2], crisp_alpha));

    if (logo_svg.empty() || input.textures == nullptr)
        return;
    const float dpi = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
    const ImTextureID crisp = input.textures->svg(logo_texture_id, logo_svg,
        static_cast<int>(std::lround(side * dpi)),
        static_cast<int>(std::lround(side * dpi)));
    const ImTextureID soft = input.textures->svg(logo_texture_id, logo_svg,
        theme::intro_glow_raster, theme::intro_glow_raster);
    const auto draw_logo = [&](ImTextureID texture, float scale, ImU32 tint) {
        const float half = side * scale * 0.5f;
        list->AddImage(texture, ImVec2(centre.x - half, centre.y - half),
            ImVec2(centre.x + half, centre.y + half), ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f), tint);
    };

    /* Outermost first, so each nearer layer lies over the fainter bloom. */
    if (soft != 0) {
        for (int i = theme::intro_glow_layers - 1; i >= 0; --i) {
            const ImVec4 c = layer_colour(i);
            draw_logo(soft,
                intro_timing::glow_scale(theme::intro_glow_scales[i], bloom),
                colour(c.x, c.y, c.z,
                    theme::intro_glow_alphas[i] * glow * veil * layer_fade));
        }
    }
    if (crisp != 0) {
        draw_logo(crisp, 1.0f,
            colour(theme::intro_core_rgb[0], theme::intro_core_rgb[1],
                theme::intro_core_rgb[2], crisp_alpha));
    }
}

}
