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
#include "allocation_counter.hpp"
#include "headless_desk.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/intro.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdint>
#include <string_view>

using hedgr::ui::render::draw_intro;
using hedgr::ui::render::intro_dark_ns;
using hedgr::ui::render::intro_peak_ns;
using hedgr::ui::render::intro_skip_requested;
using hedgr::ui::render::intro_timing;
using hedgr::ui::render::intro_total_ns;

namespace {

constexpr std::int64_t step_ns = 10'000'000;
constexpr std::int64_t start_ns = 1'000'000'000;
constexpr std::uint64_t wallpaper_texture = 3;
constexpr std::string_view logo = "<svg/>";

/*
 * The intro's whole behaviour is a function of the time since it began, so the
 * curve tests state times and read values. The frame tests drive the same time
 * through draw_intro, which is the clock hook: the application passes the
 * elapsed time and nothing else.
 */
struct intro_frame final {
    float light = 0.0f;
    float alpha_sum = 0.0f;
    int logo_quads = 0;
    int vertices = 0;
};

/** @brief Draws the intro at @p elapsed_ns and reads what it put in the list. */
intro_frame read_intro(hedgr::testing::desk& desk, std::int64_t elapsed_ns,
    std::string_view svg = logo, std::uint64_t wallpaper = wallpaper_texture) {
    ImGui::NewFrame();
    auto input = desk.input();
    input.background_texture = wallpaper;
    draw_intro(input, svg, elapsed_ns);
    const ImDrawList* list = ImGui::GetForegroundDrawList();
    intro_frame out;
    out.vertices = list->VtxBuffer.Size;
    for (const ImDrawVert& v : list->VtxBuffer) {
        const float a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
            / 255.0f;
        const float rgb = static_cast<float>((v.col >> IM_COL32_R_SHIFT) & 0xff)
            + static_cast<float>((v.col >> IM_COL32_G_SHIFT) & 0xff)
            + static_cast<float>((v.col >> IM_COL32_B_SHIFT) & 0xff);
        out.light += rgb * a;
        out.alpha_sum += a;
    }
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
        if (cmd.GetTexID() == static_cast<ImTextureID>(7))
            out.logo_quads += static_cast<int>(cmd.ElemCount) / 6;
    }
    ImGui::EndFrame();
    return out;
}

}

TEST_CASE("the intro's timeline is the one the sound is cut to", "[intro]") {
    CHECK(intro_peak_ns == 1'400'000'000);
    CHECK(intro_dark_ns == 2'400'000'000);
    CHECK(intro_total_ns == 3'200'000'000);
}

TEST_CASE("the logo starts dark, peaks at the peak and is dark again",
    "[intro]") {
    CHECK(intro_timing::brightness(0) == 0.0f);
    CHECK(intro_timing::brightness(intro_peak_ns) == 1.0f);
    CHECK(intro_timing::brightness(intro_dark_ns) == 0.0f);
    CHECK(intro_timing::brightness(intro_total_ns) == 0.0f);
    CHECK(intro_timing::brightness(intro_peak_ns / 2) < 0.5f);
    CHECK(intro_timing::brightness(intro_peak_ns / 2) > 0.0f);
}

TEST_CASE("the brightness only rises to the peak and only falls after it",
    "[intro]") {
    float previous = 0.0f;
    for (std::int64_t t = step_ns; t <= intro_peak_ns; t += step_ns) {
        const float now = intro_timing::brightness(t);
        CHECK(now > previous);
        CHECK(now <= 1.0f);
        previous = now;
    }
    for (std::int64_t t = intro_peak_ns + step_ns; t < intro_dark_ns;
         t += step_ns) {
        const float now = intro_timing::brightness(t);
        CHECK(now < previous);
        CHECK(now >= 0.0f);
        previous = now;
    }
}

TEST_CASE("the brightness clamps outside the timeline", "[intro]") {
    CHECK(intro_timing::brightness(-1) == 0.0f);
    CHECK(intro_timing::brightness(-intro_total_ns) == 0.0f);
    CHECK(intro_timing::brightness(intro_total_ns * 10) == 0.0f);
}

TEST_CASE("the veil holds through the glow and only fades after the dip",
    "[intro]") {
    CHECK(intro_timing::veil_alpha(0) == 1.0f);
    CHECK(intro_timing::veil_alpha(intro_peak_ns) == 1.0f);
    CHECK(intro_timing::veil_alpha((intro_peak_ns + intro_dark_ns) / 2) == 1.0f);
    CHECK(intro_timing::veil_alpha(intro_dark_ns) == 1.0f);
    CHECK(intro_timing::veil_alpha(intro_dark_ns + step_ns) < 1.0f);
    CHECK(intro_timing::veil_alpha(intro_total_ns) == 0.0f);

    float previous = 1.0f;
    for (std::int64_t t = intro_dark_ns; t <= intro_total_ns; t += step_ns) {
        const float now = intro_timing::veil_alpha(t);
        CHECK(now <= previous);
        previous = now;
    }
}

TEST_CASE("the veil clamps outside the timeline", "[intro]") {
    CHECK(intro_timing::veil_alpha(-1) == 1.0f);
    CHECK(intro_timing::veil_alpha(intro_total_ns * 10) == 0.0f);
}

TEST_CASE("the intro ends at its total or when it is skipped", "[intro]") {
    CHECK_FALSE(intro_timing::finished(0, false));
    CHECK_FALSE(intro_timing::finished(intro_total_ns - 1, false));
    CHECK(intro_timing::finished(intro_total_ns, false));
    CHECK(intro_timing::finished(0, true));
    CHECK(intro_timing::finished(intro_total_ns - 1, true));
}

TEST_CASE("an intro is active from its start until its end or a skip",
    "[intro]") {
    intro_timing timing;
    timing.started_ns = start_ns;
    CHECK(timing.active(start_ns));
    CHECK(timing.active(start_ns + intro_total_ns - 1));
    CHECK_FALSE(timing.active(start_ns + intro_total_ns));

    timing.skipped = true;
    CHECK_FALSE(timing.active(start_ns));
}

TEST_CASE("a key or a click asks to skip, a scroll does not", "[intro]") {
    {
        hedgr::testing::headless_imgui imgui;
        ImGui::NewFrame();
        CHECK_FALSE(intro_skip_requested());
        ImGui::EndFrame();
    }
    {
        hedgr::testing::headless_imgui imgui;
        ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
        ImGui::NewFrame();
        CHECK(intro_skip_requested());
        ImGui::EndFrame();
    }
    {
        hedgr::testing::headless_imgui imgui;
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        ImGui::NewFrame();
        CHECK(intro_skip_requested());
        ImGui::EndFrame();
    }
    {
        hedgr::testing::headless_imgui imgui;
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
        ImGui::NewFrame();
        CHECK_FALSE(intro_skip_requested());
        ImGui::EndFrame();
    }
}

TEST_CASE("the intro is darkest at the start and brightest at the peak",
    "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto first = read_intro(desk, 0);
    const auto rising = read_intro(desk, intro_peak_ns / 2);
    const auto peak = read_intro(desk, intro_peak_ns);
    const auto falling = read_intro(desk, (intro_peak_ns + intro_dark_ns) / 2);
    const auto dark = read_intro(desk, intro_dark_ns);

    CHECK(first.light < rising.light);
    CHECK(rising.light < peak.light);
    CHECK(falling.light < peak.light);
    CHECK(dark.light < falling.light);
    CHECK(dark.light == first.light);
}

TEST_CASE("the veil's vertices fade out after the dip", "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto held = read_intro(desk, intro_dark_ns);
    const auto fading = read_intro(desk, (intro_dark_ns + intro_total_ns) / 2);
    const auto gone = read_intro(desk, intro_total_ns);

    CHECK(fading.alpha_sum < held.alpha_sum);
    CHECK(gone.alpha_sum < fading.alpha_sum);
    CHECK(gone.alpha_sum == 0.0f);
}

TEST_CASE("the glow draws the configured layers and the crisp logo", "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    CHECK(read_intro(desk, 0).logo_quads == 0);
    CHECK(read_intro(desk, intro_peak_ns).logo_quads
        == hedgr::ui::theme::intro_glow_layers + 1);
}

TEST_CASE("without a logo document the wallpaper and halo still draw",
    "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto frame = read_intro(desk, intro_peak_ns, "");
    CHECK(frame.logo_quads == 0);
    CHECK(frame.vertices > 4);
}

TEST_CASE("a steady intro frame costs no allocation", "[intro][frame]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto draw = [&] {
        ImGui::NewFrame();
        auto input = desk.input();
        input.background_texture = wallpaper_texture;
        draw_intro(input, logo, intro_peak_ns);
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    CHECK(hedgr::testing::allocations() - before == 0);
}

namespace {

/** @brief What the word put in the list: the lit glyph vertices under the swirl. */
struct word_read final {
    int vertices = 0;
    /** @brief Every vertex the intro submitted, lit or not. */
    int submitted = 0;
    float alpha_sum = 0.0f;
    float min_x = 1e9f;
    float max_x = -1e9f;
    /** @brief The widest layer's advance width: the box the word is centred in. */
    float advance = 0.0f;
};

/**
 * @brief Draws the intro with no logo document, so the only lit vertices under
 * the swirl's lower edge are the word's glyphs.
 *
 * The halo's rim is transparent, its centre is above the line, the vignette is
 * black and the wallpaper is off, so the filter below keeps glyphs and nothing
 * else.
 */
word_read read_word(hedgr::testing::desk& desk, std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    auto input = desk.input();
    input.background_texture = 0;
    draw_intro(input, "", elapsed_ns);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float side = std::min(viewport->Size.x, viewport->Size.y)
        * hedgr::ui::theme::intro_logo_fraction;
    const float line = viewport->Size.y * 0.5f + side * 0.5f;
    word_read out;
    out.submitted = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    for (const ImDrawVert& v : ImGui::GetForegroundDrawList()->VtxBuffer) {
        const float a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
            / 255.0f;
        const bool lit = (v.col & 0x00ffffffu) != 0 && a > 0.0f;
        if (!lit || v.pos.y <= line)
            continue;
        ++out.vertices;
        out.alpha_sum += a;
        out.min_x = std::min(out.min_x, v.pos.x);
        out.max_x = std::max(out.max_x, v.pos.x);
    }
    const float size = side * hedgr::ui::theme::intro_word_size
        * hedgr::ui::theme::intro_glow_scales[hedgr::ui::theme::intro_glow_layers - 1];
    out.advance = hedgr::ui::icon::bold_font()->CalcTextSizeA(size, 1e9f, 0.0f,
        hedgr::ui::theme::intro_word).x;
    ImGui::EndFrame();
    return out;
}

}

TEST_CASE("the word is dark at the start and drawn at the peak", "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    CHECK(read_word(desk, 0).vertices == 0);
    /* Dark means nothing is submitted for it: the black fill and the vignette only. */
    CHECK(read_word(desk, 0).submitted == 4 + 33);
    CHECK(read_word(desk, intro_dark_ns).vertices == 0);
    CHECK(read_word(desk, intro_peak_ns).vertices > 0);
}

TEST_CASE("the word takes the logo's brightness", "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_word(desk, intro_peak_ns);
    const std::int64_t at = intro_peak_ns * 4 / 5;
    const auto rising = read_word(desk, at);
    REQUIRE(peak.alpha_sum > 0.0f);
    CHECK(rising.vertices == peak.vertices);
    CHECK(rising.alpha_sum / peak.alpha_sum
        == Catch::Approx(intro_timing::brightness(at)).epsilon(0.06));

    const auto falling = read_word(desk,
        (intro_peak_ns + intro_dark_ns) / 2);
    CHECK(falling.alpha_sum < peak.alpha_sum);
}

TEST_CASE("the word is centred on the viewport", "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_word(desk, intro_peak_ns);
    REQUIRE(peak.vertices > 0);
    const float centre = ImGui::GetIO().DisplaySize.x * 0.5f;
    /*
     * The ink is not symmetric in its advance box, because the full stop's
     * glyph sits at the left of its cell, so the box is the measure: the
     * outermost layer's left edge is half its advance left of the centre, and
     * no ink passes the box's right edge.
     */
    const float margin = peak.advance * 0.1f;
    CHECK(peak.min_x == Catch::Approx(centre - peak.advance * 0.5f).margin(margin));
    CHECK(peak.max_x <= centre + peak.advance * 0.5f + 1.0f);
}
