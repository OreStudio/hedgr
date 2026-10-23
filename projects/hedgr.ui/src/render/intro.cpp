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

/** @brief The crisp shapes' colour at @p brightness: the rest grey-white, climbing to the core. */
[[nodiscard]] ImVec4 crisp_colour(float brightness) {
    namespace theme = hedgr::ui::theme;
    const float lift = hedgr::ui::render::intro_timing::lift(brightness);
    const auto blend = [&](int k) {
        return theme::intro_rest_rgb[k]
            + (theme::intro_core_rgb[k] - theme::intro_rest_rgb[k]) * lift;
    };
    return ImVec4(blend(0), blend(1), blend(2), 1.0f);
}

/**
 * @brief The halo colour of layer @p index at @p brightness.
 *
 * Indigo as it first lights, moving to the layer's colour at the peak: the core for the
 * tight layer, shading toward the chart purple the wider it is.
 */
[[nodiscard]] ImVec4 halo_colour(int index, float brightness) {
    namespace theme = hedgr::ui::theme;
    const ImVec4 purple = theme::chart_line_colour();
    const float mix = theme::intro_glow_purple_mix * static_cast<float>(index)
        / static_cast<float>(theme::intro_glow_layers - 1);
    const float peak[3] = {
        theme::intro_core_rgb[0] + (purple.x - theme::intro_core_rgb[0]) * mix,
        theme::intro_core_rgb[1] + (purple.y - theme::intro_core_rgb[1]) * mix,
        theme::intro_core_rgb[2] + (purple.z - theme::intro_core_rgb[2]) * mix};
    const auto blend = [&](int k) {
        return theme::intro_halo_cold_rgb[k]
            + (peak[k] - theme::intro_halo_cold_rgb[k])
                * hedgr::ui::render::intro_timing::lift(brightness);
    };
    return ImVec4(blend(0), blend(1), blend(2), 1.0f);
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
    const float veil = intro_timing::veil_alpha(elapsed_ns);
    const float glow = intro_timing::brightness(elapsed_ns);
    const float halo = intro_timing::halo_gain(glow) * veil;
    const float crisp_alpha = theme::intro_peak_intensity * veil;
    const ImVec4 lit = crisp_colour(glow);

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
    radial_fan(list,
        ImVec2(origin.x + viewport->Size.x * 0.5f,
            origin.y + viewport->Size.y * 0.5f),
        0.5f * std::hypot(viewport->Size.x, viewport->Size.y),
        colour(0.0f, 0.0f, 0.0f, 0.0f),
        colour(0.0f, 0.0f, 0.0f, theme::intro_vignette_alpha * veil));

    /*
     * The lockup: the swirl over the word, the pair centred a little above the
     * middle. The font size is whatever makes the word as wide as asked.
     */
    ImFont* const font = icon::bold_font() != nullptr ? icon::bold_font()
                                                       : ImGui::GetFont();
    const float side = std::min(viewport->Size.x, viewport->Size.y)
        * theme::intro_logo_fraction;
    constexpr float probe_size = 100.0f;
    const float probe_width = font->CalcTextSizeA(probe_size, FLT_MAX, 0.0f,
        theme::intro_word).x;
    const float word_size = probe_width > 0.0f
        ? theme::intro_word_width * side * probe_size / probe_width
        : side * 0.2f;
    const float lockup = side * (1.0f + theme::intro_word_gap) + word_size;
    const float top = origin.y + viewport->Size.y * (0.5f - theme::intro_lockup_lift)
        - lockup * 0.5f;
    const ImVec2 logo_centre(origin.x + viewport->Size.x * 0.5f, top + side * 0.5f);
    const ImVec2 word_centre(logo_centre.x,
        top + side * (1.0f + theme::intro_word_gap) + word_size * 0.5f);

    const ImVec2 word_extent = font->CalcTextSizeA(word_size, FLT_MAX, 0.0f,
        theme::intro_word);
    const auto draw_word = [&](ImVec2 offset, ImU32 tint) {
        list->AddText(font, word_size,
            ImVec2(word_centre.x - word_extent.x * 0.5f + offset.x,
                word_centre.y - word_extent.y * 0.5f + offset.y),
            tint, theme::intro_word);
    };
    /*
     * The word's glow: rings of copies at tiny offsets, widest first, each at
     * a very low alpha, which sums to a soft blur. The offsets are fixed: only
     * the alpha and colour follow the pulse.
     */
    const ImVec4 word_glow = halo_colour(1, glow);
    constexpr float two_pi = 6.2831853f;
    for (int ring = theme::intro_word_rings - 1; ring >= 0 && halo > 0.0f; --ring) {
        const int copies = theme::intro_word_ring_copies[ring];
        const float radius = word_size * theme::intro_word_ring_radii[ring];
        const ImU32 tint = colour(word_glow.x, word_glow.y, word_glow.z,
            theme::intro_word_ring_alphas[ring] * halo);
        const float turn = 0.5f * static_cast<float>(ring);
        for (int k = 0; k < copies; ++k) {
            const float angle = turn + two_pi * static_cast<float>(k)
                / static_cast<float>(copies);
            draw_word(ImVec2(std::cos(angle) * radius, std::sin(angle) * radius),
                tint);
        }
    }
    draw_word(ImVec2(0.0f, 0.0f), colour(lit.x, lit.y, lit.z, crisp_alpha));

    if (logo_svg.empty() || input.textures == nullptr)
        return;
    const float dpi = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
    const ImTextureID crisp = input.textures->svg(logo_texture_id, logo_svg,
        static_cast<int>(std::lround(side * dpi)),
        static_cast<int>(std::lround(side * dpi)));
    const auto draw_logo = [&](ImTextureID texture, float scale, ImU32 tint) {
        const float half = side * scale * 0.5f;
        list->AddImage(texture,
            ImVec2(logo_centre.x - half, logo_centre.y - half),
            ImVec2(logo_centre.x + half, logo_centre.y + half),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
    };

    /*
     * The glow textures hold the swirl at inner/raster of their side, so each
     * is drawn raster/inner times larger than the swirl, at that one size for
     * the whole intro. Widest first, so the tight layer lies over the haze.
     */
    constexpr float canvas = static_cast<float>(theme::intro_glow_raster)
        / static_cast<float>(theme::intro_glow_inner);
    for (int i = theme::intro_glow_layers - 1; i >= 0 && halo > 0.0f; --i) {
        const ImTextureID soft = input.textures->svg_glow(logo_texture_id,
            logo_svg, theme::intro_glow_raster, theme::intro_glow_inner,
            theme::intro_glow_sigmas[i]);
        if (soft == 0)
            continue;
        const ImVec4 c = halo_colour(i, glow);
        draw_logo(soft, canvas,
            colour(c.x, c.y, c.z, theme::intro_glow_alphas[i] * halo));
    }
    if (crisp != 0) {
        draw_logo(crisp, 1.0f, colour(lit.x, lit.y, lit.z, crisp_alpha));
    }
}

}
