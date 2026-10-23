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
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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
    /* The peak is the loudest sample of intro_swell.wav, measured: 123281 / 44100 s. */
    CHECK(intro_peak_ns == 2'795'000'000);
    CHECK(intro_dark_ns == 5'200'000'000);
    CHECK(intro_total_ns == 6'000'000'000);
}

TEST_CASE("the logo starts dark, peaks at the peak and is dark again",
    "[intro]") {
    CHECK(intro_timing::brightness(0) == 0.0f);
    CHECK(intro_timing::brightness(intro_peak_ns) == 1.0f);
    CHECK(intro_timing::brightness(intro_dark_ns) == 0.0f);
    CHECK(intro_timing::brightness(intro_total_ns) == 0.0f);
    CHECK(intro_timing::brightness(intro_peak_ns / 2)
        == Catch::Approx(0.5f).margin(0.01));
}

TEST_CASE("the brightness eases: a gentle start and a gentle landing",
    "[intro]") {
    CHECK(intro_timing::brightness(step_ns) < 0.0001f);
    CHECK(intro_timing::brightness(intro_peak_ns - step_ns) > 0.9999f);
    CHECK(intro_timing::brightness(intro_dark_ns - step_ns) < 0.0001f);
}

TEST_CASE("the brightness never jumps between samples 10 ms apart", "[intro]") {
    float biggest = 0.0f;
    float previous = intro_timing::brightness(0);
    for (std::int64_t t = step_ns; t <= intro_total_ns; t += step_ns) {
        const float now = intro_timing::brightness(t);
        biggest = std::max(biggest, std::abs(now - previous));
        previous = now;
    }
    CHECK(biggest < hedgr::ui::render::intro_max_step);
    /* A bound that loose would let a slow pulse hide a step, so it is also tight. */
    CHECK(biggest > hedgr::ui::render::intro_max_step * 0.3f);
}

TEST_CASE("the expansion follows the brightness and clamps with it", "[intro]") {
    for (std::int64_t t = -step_ns; t <= intro_total_ns + step_ns; t += step_ns)
        CHECK(intro_timing::expansion(t) == intro_timing::brightness(t));
    CHECK(intro_timing::expansion(0) == 0.0f);
    CHECK(intro_timing::expansion(intro_peak_ns) == 1.0f);
    CHECK(intro_timing::expansion(intro_total_ns) == 0.0f);
    CHECK(intro_timing::expansion(-1) == 0.0f);
}

TEST_CASE("a glow layer reaches out as the expansion grows", "[intro]") {
    const float full = hedgr::ui::theme::intro_glow_scales[5];
    CHECK(intro_timing::glow_scale(full, 1.0f) == Catch::Approx(full));
    CHECK(intro_timing::glow_scale(full, 0.0f) < intro_timing::glow_scale(full, 0.5f));
    CHECK(intro_timing::glow_scale(full, 0.5f) < intro_timing::glow_scale(full, 1.0f));
    CHECK(intro_timing::glow_scale(1.0f, 0.5f) == 1.0f);
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

TEST_CASE("the pulse has exactly one maximum", "[intro]") {
    int maxima = 0;
    for (std::int64_t t = 0; t <= intro_total_ns; t += step_ns)
        if (intro_timing::brightness(t) >= 0.9999f)
            ++maxima;
    /* The plateau of the smoothstep is a few samples wide and contiguous. */
    CHECK(maxima >= 1);
    CHECK(maxima <= 3);
    CHECK(intro_timing::brightness(intro_peak_ns) == 1.0f);
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
    CHECK(read_intro(desk, intro_peak_ns / 3).logo_quads
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
    /** @brief The width of the outermost glow layer, the first one drawn. */
    float outer_width = 0.0f;
    /** @brief The widest layer's advance width: the box the word is centred in. */
    float advance = 0.0f;
};

/*
 * ImGui drops a draw whose alpha rounds to 0, so the faintest layers vanish
 * near the dark ends. The shape tests sample where every layer is still drawn:
 * from 3/10 of the way up the ramp to 7/10 of the way down it.
 */
constexpr int first_tenth = 3;
constexpr int last_falling_tenth = 7;

/** @brief The black fill, the halo's fan and the vignette's fan come before any text. */
constexpr int under_text_vertices = 4 + 33 + 33;

/**
 * @brief Draws the intro with no logo document, so the lit vertices after the
 * halo and vignette are the word's glyphs, glow layers first.
 */
word_read read_word(hedgr::testing::desk& desk, std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    auto input = desk.input();
    input.background_texture = 0;
    draw_intro(input, "", elapsed_ns);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float side = std::min(viewport->Size.x, viewport->Size.y)
        * hedgr::ui::theme::intro_logo_fraction;
    word_read out;
    out.submitted = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    std::vector<float> xs;
    int index = 0;
    for (const ImDrawVert& v : ImGui::GetForegroundDrawList()->VtxBuffer) {
        const float a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
            / 255.0f;
        const bool lit = (v.col & 0x00ffffffu) != 0 && a > 0.0f;
        if (index++ < under_text_vertices || !lit)
            continue;
        xs.push_back(v.pos.x);
        ++out.vertices;
        out.alpha_sum += a;
        out.min_x = std::min(out.min_x, v.pos.x);
        out.max_x = std::max(out.max_x, v.pos.x);
    }
    /* Seven draws of the same text, outermost first: the glow layers, then the crisp. */
    if (!xs.empty()) {
        const std::size_t per_layer = xs.size()
            / (hedgr::ui::theme::intro_glow_layers + 1);
        const auto outer = std::minmax_element(xs.begin(),
            xs.begin() + static_cast<std::ptrdiff_t>(per_layer));
        out.outer_width = *outer.second - *outer.first;
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
    REQUIRE(peak.alpha_sum > 0.0f);
    float previous = 0.0f;
    for (int tenth = first_tenth; tenth < 10; ++tenth) {
        const auto rising = read_word(desk, intro_peak_ns * tenth / 10);
        CHECK(rising.vertices == peak.vertices);
        CHECK(rising.alpha_sum > previous);
        CHECK(rising.alpha_sum < peak.alpha_sum);
        previous = rising.alpha_sum;
    }
    const auto falling = read_word(desk,
        (intro_peak_ns + intro_dark_ns) / 2);
    CHECK(falling.alpha_sum < peak.alpha_sum);
}

TEST_CASE("the word's glow spreads outwards with the pulse and draws back in",
    "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    float previous = 0.0f;
    for (int tenth = first_tenth; tenth <= 10; ++tenth) {
        const float width = read_word(desk, intro_peak_ns * tenth / 10).outer_width;
        CHECK(width > previous);
        previous = width;
    }
    for (int tenth = 1; tenth <= last_falling_tenth; ++tenth) {
        const float width = read_word(desk,
            intro_peak_ns + (intro_dark_ns - intro_peak_ns) * tenth / 10)
                                .outer_width;
        CHECK(width < previous);
        previous = width;
    }
}

TEST_CASE("the word is centred on the viewport", "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_word(desk, intro_peak_ns);
    REQUIRE(peak.vertices > 0);
    CHECK(peak.outer_width == Catch::Approx(peak.max_x - peak.min_x).margin(1.0));
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

namespace {

struct quad_extent final {
    float width = 0.0f;
    float height = 0.0f;
};

/** @brief Every logo quad the intro drew, in draw order: the glow layers, then the crisp one. */
std::vector<quad_extent> read_logo_quads(hedgr::testing::desk& desk,
    std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    draw_intro(desk.input(), logo, elapsed_ns);
    const ImDrawList* list = ImGui::GetForegroundDrawList();
    std::vector<quad_extent> out;
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
        if (cmd.GetTexID() != static_cast<ImTextureID>(7))
            continue;
        for (unsigned i = 0; i + 6 <= cmd.ElemCount; i += 6) {
            float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
            for (unsigned k = 0; k < 6; ++k) {
                const ImDrawVert& v = list->VtxBuffer[static_cast<int>(
                    cmd.VtxOffset + list->IdxBuffer[static_cast<int>(
                        cmd.IdxOffset + i + k)])];
                lo_x = std::min(lo_x, v.pos.x);
                hi_x = std::max(hi_x, v.pos.x);
                lo_y = std::min(lo_y, v.pos.y);
                hi_y = std::max(hi_y, v.pos.y);
            }
            out.push_back({hi_x - lo_x, hi_y - lo_y});
        }
    }
    ImGui::EndFrame();
    return out;
}

}

TEST_CASE("the logo's glow spreads outwards with the pulse and draws back in",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    float previous = 0.0f;
    for (int tenth = first_tenth; tenth <= 10; ++tenth) {
        const auto quads = read_logo_quads(desk, intro_peak_ns * tenth / 10);
        REQUIRE(quads.size() == hedgr::ui::theme::intro_glow_layers + 1);
        CHECK(quads.front().width > previous);
        previous = quads.front().width;
    }
    for (int tenth = 1; tenth <= last_falling_tenth; ++tenth) {
        const auto quads = read_logo_quads(desk,
            intro_peak_ns + (intro_dark_ns - intro_peak_ns) * tenth / 10);
        REQUIRE_FALSE(quads.empty());
        CHECK(quads.front().width < previous);
        previous = quads.front().width;
    }
}

TEST_CASE("the glow's outermost layer reaches the configured scale at the peak",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_logo_quads(desk, intro_peak_ns);
    const auto early = read_logo_quads(desk, intro_peak_ns * first_tenth / 10);
    REQUIRE(peak.size() == hedgr::ui::theme::intro_glow_layers + 1);
    REQUIRE(early.size() == peak.size());
    const float crisp = peak.back().width;
    CHECK(peak.front().width / crisp
        == Catch::Approx(hedgr::ui::theme::intro_glow_scales[5]).epsilon(0.01));
    CHECK(early.front().width / crisp < 1.0f + 0.5f * (peak.front().width / crisp - 1.0f));
}

TEST_CASE("the crisp logo keeps its size through the whole pulse",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const float reference = read_logo_quads(desk, intro_peak_ns).back().width;
    REQUIRE(reference > 0.0f);
    const std::int64_t last = intro_peak_ns
        + (intro_dark_ns - intro_peak_ns) * last_falling_tenth / 10;
    for (std::int64_t t = intro_peak_ns * first_tenth / 10; t <= last;
         t += intro_total_ns / 40) {
        const auto quads = read_logo_quads(desk, t);
        REQUIRE_FALSE(quads.empty());
        CHECK(quads.back().width == Catch::Approx(reference).epsilon(0.02));
        CHECK(quads.back().height == Catch::Approx(reference).epsilon(0.02));
    }
}

namespace {

/** @brief The brightest any one channel of any drawn vertex is, after its alpha. */
float brightest_channel(hedgr::testing::desk& desk, std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    draw_intro(desk.input(), logo, elapsed_ns);
    float top = 0.0f;
    for (const ImDrawVert& v : ImGui::GetForegroundDrawList()->VtxBuffer) {
        const float a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
            / 255.0f;
        for (const int shift : {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT,
                 IM_COL32_B_SHIFT})
            top = std::max(top,
                static_cast<float>((v.col >> shift) & 0xff) / 255.0f * a);
    }
    ImGui::EndFrame();
    return top;
}

/** @brief The first version drew the crisp logo at full alpha in a 255 blue channel. */
constexpr float first_version_peak_channel = 1.0f;
/** @brief The most any channel may reach at the peak: bright, never clipped. */
constexpr float max_peak_channel = 0.82f;

}

TEST_CASE("the peak glows softly: no channel clips and it is dimmer than the first version",
    "[intro][brightness]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const float peak = brightest_channel(desk, intro_peak_ns);
    CHECK(peak <= max_peak_channel);
    CHECK(peak < first_version_peak_channel);
    CHECK(peak > 0.5f);
    CHECK(brightest_channel(desk, intro_peak_ns) >= brightest_channel(desk, intro_peak_ns / 2));
}
