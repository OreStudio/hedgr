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
#include "hedgr.ui/image/svg_raster.hpp"
#include "hedgr.ui/render/intro.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "software_raster.hpp"
#include <algorithm>
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <string>
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
 * ImGui drops a draw whose alpha rounds to 0, so the faintest layers vanish
 * near the dark ends. The shape tests sample where every layer is still drawn:
 * from 3/10 of the way up the ramp to 7/10 of the way down it.
 */
constexpr int first_tenth = 3;
constexpr int last_falling_tenth = 7;
/** @brief The first tenth up the ramp at which every ring copy of the word is drawn. */
constexpr int word_first_tenth = 5;


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
    int glow_quads = 0;
    /** @brief The summed vertex alpha of the blurred glow textures. */
    float glow_alpha = 0.0f;
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
        if (cmd.GetTexID() != static_cast<ImTextureID>(8))
            continue;
        out.glow_quads += static_cast<int>(cmd.ElemCount) / 6;
        for (unsigned i = 0; i < cmd.ElemCount; ++i) {
            const ImDrawVert& v = list->VtxBuffer[static_cast<int>(
                cmd.VtxOffset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)])];
            out.glow_alpha += static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
                / 255.0f;
        }
    }
    ImGui::EndFrame();
    return out;
}

}

TEST_CASE("the intro's timeline is the one the sound is cut to", "[intro]") {
    /* The peak is the centre of the loudest 50 ms window of intro_swell.wav, measured. */
    CHECK(intro_peak_ns == 3'023'000'000);
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

TEST_CASE("the glow draws the blurred layers and the crisp logo", "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    CHECK(read_intro(desk, 0).logo_quads == 0);
    CHECK(read_intro(desk, 0).glow_quads == 0);
    for (const auto t : {intro_peak_ns, intro_peak_ns / 3}) {
        const auto frame = read_intro(desk, t);
        CHECK(frame.logo_quads == 1);
        CHECK(frame.glow_quads == hedgr::ui::theme::intro_glow_layers);
    }
}

TEST_CASE("the glow's intensity is nothing when dark and greatest at the peak",
    "[intro][glow]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const float peak = read_intro(desk, intro_peak_ns).glow_alpha;
    REQUIRE(peak > 0.0f);
    CHECK(read_intro(desk, 0).glow_alpha == 0.0f);
    CHECK(read_intro(desk, intro_dark_ns).glow_alpha == 0.0f);
    CHECK(read_intro(desk, intro_total_ns).glow_alpha == 0.0f);
    float previous = 0.0f;
    for (int tenth = first_tenth; tenth < 10; ++tenth) {
        const float rising = read_intro(desk, intro_peak_ns * tenth / 10).glow_alpha;
        CHECK(rising > previous);
        CHECK(rising < peak);
        previous = rising;
    }
    CHECK(read_intro(desk, (intro_peak_ns + intro_dark_ns) / 2).glow_alpha < peak);
    /* Barely visible a hundredth of the pulse in, and never more than the peak. */
    CHECK(read_intro(desk, intro_peak_ns / 20).glow_alpha < peak * 0.02f);
}

TEST_CASE("without a logo document the wallpaper and word still draw",
    "[intro]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto frame = read_intro(desk, intro_peak_ns, "");
    CHECK(frame.logo_quads == 0);
    CHECK(frame.glow_quads == 0);
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
    /** @brief The width of the outermost ring of copies, the first drawn. */
    float outer_width = 0.0f;
    /** @brief The crisp word's ink, the last copy drawn. */
    float crisp_min_x = 1e9f;
    float crisp_max_x = -1e9f;
    float crisp_min_y = 1e9f;
    float crisp_max_y = -1e9f;
};

/** @brief How many draws of the word there are: every ring's copies, then the crisp. */
constexpr int word_draws = 10 + 12 + 14 + 1;

/** @brief The black fill and the vignette's fan come before any text. */
constexpr int under_text_vertices = 4 + 33;

/**
 * @brief Draws the intro with no logo document, so the lit vertices after the
 * black fill and vignette are the word's glyphs, the rings first.
 */
word_read read_word(hedgr::testing::desk& desk, std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    auto input = desk.input();
    input.background_texture = 0;
    draw_intro(input, "", elapsed_ns);
    word_read out;
    out.submitted = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    std::vector<ImDrawVert> lit;
    int index = 0;
    for (const ImDrawVert& v : ImGui::GetForegroundDrawList()->VtxBuffer) {
        const float a = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xff)
            / 255.0f;
        if (index++ < under_text_vertices || (v.col & 0x00ffffffu) == 0 || a <= 0.0f)
            continue;
        lit.push_back(v);
        ++out.vertices;
        out.alpha_sum += a;
        out.min_x = std::min(out.min_x, v.pos.x);
        out.max_x = std::max(out.max_x, v.pos.x);
    }
    if (!lit.empty()) {
        const std::size_t per_draw = lit.size() / word_draws;
        float lo = 1e9f;
        float hi = -1e9f;
        for (std::size_t i = 0; i < per_draw * 14; ++i) {
            lo = std::min(lo, lit[i].pos.x);
            hi = std::max(hi, lit[i].pos.x);
        }
        out.outer_width = hi - lo;
        for (std::size_t i = lit.size() - per_draw; i < lit.size(); ++i) {
            out.crisp_min_x = std::min(out.crisp_min_x, lit[i].pos.x);
            out.crisp_max_x = std::max(out.crisp_max_x, lit[i].pos.x);
            out.crisp_min_y = std::min(out.crisp_min_y, lit[i].pos.y);
            out.crisp_max_y = std::max(out.crisp_max_y, lit[i].pos.y);
        }
    }
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
    for (int tenth = word_first_tenth; tenth < 10; ++tenth) {
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

/*
 * The ring copies' alpha follows the brightness squared, so the faintest round
 * to nothing and ImGui drops them near the dark ends. The word's geometry is
 * compared where every copy is still drawn.
 */
TEST_CASE("the word and its halo keep one fixed size through the whole pulse",
    "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const std::int64_t times[] = {intro_peak_ns * word_first_tenth / 10,
        intro_peak_ns,
        intro_peak_ns + (intro_dark_ns - intro_peak_ns) * 3 / 10};
    const auto reference = read_word(desk, intro_peak_ns);
    REQUIRE(reference.vertices > 0);
    for (const std::int64_t t : times) {
        const auto frame = read_word(desk, t);
        CHECK(frame.vertices == reference.vertices);
        CHECK(frame.outer_width == Catch::Approx(reference.outer_width).margin(0.5));
        CHECK(frame.min_x == Catch::Approx(reference.min_x).margin(0.5));
        CHECK(frame.max_x == Catch::Approx(reference.max_x).margin(0.5));
        CHECK(frame.crisp_min_x == Catch::Approx(reference.crisp_min_x).margin(0.5));
        CHECK(frame.crisp_max_x == Catch::Approx(reference.crisp_max_x).margin(0.5));
        CHECK(frame.crisp_min_y == Catch::Approx(reference.crisp_min_y).margin(0.5));
        CHECK(frame.crisp_max_y == Catch::Approx(reference.crisp_max_y).margin(0.5));
    }
}

/** @brief The swirl's side at the test's 1280 by 800 viewport. */
float swirl_side() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    return std::min(size.x, size.y) * hedgr::ui::theme::intro_logo_fraction;
}

TEST_CASE("the word is centred horizontally and as wide as asked", "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_word(desk, intro_peak_ns);
    REQUIRE(peak.vertices > 0);
    const float centre = ImGui::GetIO().DisplaySize.x * 0.5f;
    /* The full stop's glyph sits left of its cell, so the ink is a little left of centre. */
    CHECK((peak.crisp_min_x + peak.crisp_max_x) * 0.5f
        == Catch::Approx(centre).margin(swirl_side() * 0.1f));
    const float ink = peak.crisp_max_x - peak.crisp_min_x;
    const float asked = hedgr::ui::theme::intro_word_width * swirl_side();
    CHECK(ink <= asked * 1.02f);
    CHECK(ink >= asked * 0.85f);
}

TEST_CASE("the word is smaller than the swirl is wide and its capitals are modest",
    "[intro][word]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_word(desk, intro_peak_ns);
    REQUIRE(peak.vertices > 0);
    const float height = peak.crisp_max_y - peak.crisp_min_y;
    /* The capitals' ink, as a fraction of the swirl: roughly a seventh to a fifth. */
    CHECK(height / swirl_side() > 0.12f);
    CHECK(height / swirl_side() < 0.30f);
}

namespace {

constexpr std::uint64_t crisp_id = 7;
constexpr std::uint64_t glow_id = 8;

struct quad_extent final {
    float width = 0.0f;
    float height = 0.0f;
    float centre_x = 0.0f;
    float centre_y = 0.0f;
    std::uint64_t texture = 0;
};

/** @brief Every logo quad the intro drew, in draw order: the glow layers, then the crisp one. */
std::vector<quad_extent> read_logo_quads(hedgr::testing::desk& desk,
    std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    draw_intro(desk.input(), logo, elapsed_ns);
    const ImDrawList* list = ImGui::GetForegroundDrawList();
    std::vector<quad_extent> out;
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
        if (cmd.GetTexID() != static_cast<ImTextureID>(crisp_id)
            && cmd.GetTexID() != static_cast<ImTextureID>(glow_id))
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
            out.push_back({hi_x - lo_x, hi_y - lo_y, (lo_x + hi_x) * 0.5f,
                (lo_y + hi_y) * 0.5f,
                static_cast<std::uint64_t>(cmd.GetTexID())});
        }
    }
    ImGui::EndFrame();
    return out;
}

constexpr int quads_per_frame = hedgr::ui::theme::intro_glow_layers + 1;

}

TEST_CASE("the glow layers use the blurred textures at the configured sigmas",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto quads = read_logo_quads(desk, intro_peak_ns);
    REQUIRE(quads.size() == quads_per_frame);
    for (std::size_t i = 0; i < quads.size() - 1; ++i)
        CHECK(quads[i].texture == glow_id);
    CHECK(quads.back().texture == crisp_id);
    /* Widest first: the widest sigma is asked for first and the tightest last. */
    CHECK(desk.textures.last_glow_sigma == hedgr::ui::theme::intro_glow_sigmas[0]);
}

TEST_CASE("every quad keeps one fixed size and place through the whole pulse",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    namespace theme = hedgr::ui::theme;

    const auto reference = read_logo_quads(desk, intro_peak_ns);
    REQUIRE(reference.size() == quads_per_frame);
    /* All three halos are drawn at the one canvas size, raster over inner swirls. */
    const float canvas = static_cast<float>(theme::intro_glow_raster)
        / static_cast<float>(theme::intro_glow_inner);
    for (std::size_t i = 0; i + 1 < reference.size(); ++i)
        CHECK(reference[i].width == Catch::Approx(canvas * reference.back().width)
                                         .epsilon(0.001));
    const std::int64_t times[] = {intro_peak_ns * first_tenth / 10,
        intro_peak_ns * 6 / 10,
        intro_peak_ns + (intro_dark_ns - intro_peak_ns) * 3 / 10,
        intro_peak_ns + (intro_dark_ns - intro_peak_ns) * last_falling_tenth / 10};
    for (const std::int64_t t : times) {
        const auto quads = read_logo_quads(desk, t);
        REQUIRE(quads.size() == reference.size());
        for (std::size_t i = 0; i < quads.size(); ++i) {
            CHECK(quads[i].width == Catch::Approx(reference[i].width).margin(0.5));
            CHECK(quads[i].height == Catch::Approx(reference[i].height).margin(0.5));
            CHECK(quads[i].centre_x == Catch::Approx(reference[i].centre_x).margin(0.5));
            CHECK(quads[i].centre_y == Catch::Approx(reference[i].centre_y).margin(0.5));
        }
    }
}

namespace {

struct quad_colour final {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

/** @brief The colour of each logo quad's first vertex: the halo layers, then the crisp swirl. */
std::vector<quad_colour> read_quad_colours(hedgr::testing::desk& desk,
    std::int64_t elapsed_ns) {
    ImGui::NewFrame();
    draw_intro(desk.input(), logo, elapsed_ns);
    const ImDrawList* list = ImGui::GetForegroundDrawList();
    std::vector<quad_colour> out;
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
        if (cmd.GetTexID() != static_cast<ImTextureID>(crisp_id)
            && cmd.GetTexID() != static_cast<ImTextureID>(glow_id))
            continue;
        for (unsigned i = 0; i + 6 <= cmd.ElemCount; i += 6) {
            const ImDrawVert& v = list->VtxBuffer[static_cast<int>(
                cmd.VtxOffset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)])];
            const auto ch = [&](int shift) {
                return static_cast<float>((v.col >> shift) & 0xff) / 255.0f;
            };
            out.push_back({ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT),
                ch(IM_COL32_B_SHIFT), ch(IM_COL32_A_SHIFT)});
        }
    }
    ImGui::EndFrame();
    return out;
}

}

TEST_CASE("the halo's alpha rises with the brightness and falls with it",
    "[intro][glow]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    for (std::size_t layer = 0; layer < hedgr::ui::theme::intro_glow_layers; ++layer) {
        float previous = 0.0f;
        for (int tenth = first_tenth; tenth <= 10; ++tenth) {
            const float alpha = read_quad_colours(desk,
                intro_peak_ns * tenth / 10)[layer].a;
            CHECK(alpha > previous);
            previous = alpha;
        }
        for (int tenth = 2; tenth <= last_falling_tenth; ++tenth) {
            const float alpha = read_quad_colours(desk,
                intro_peak_ns + (intro_dark_ns - intro_peak_ns) * tenth / 10)[layer].a;
            CHECK(alpha < previous);
            previous = alpha;
        }
    }
}

TEST_CASE("the halo ignites late: at 30 percent of the pulse it is under a tenth of its peak",
    "[intro][glow]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_quad_colours(desk, intro_peak_ns);
    const auto low = read_quad_colours(desk, intro_peak_ns * first_tenth / 10);
    REQUIRE(peak.size() == quads_per_frame);
    REQUIRE(low.size() == quads_per_frame);
    for (std::size_t layer = 0; layer + 1 < peak.size(); ++layer)
        CHECK(low[layer].a < peak[layer].a * 0.1f);
}

TEST_CASE("the halo is indigo when dim and cyan-white at the peak, the core brightest",
    "[intro][glow]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto peak = read_quad_colours(desk, intro_peak_ns);
    const auto low = read_quad_colours(desk, intro_peak_ns * first_tenth / 10);
    REQUIRE(peak.size() == quads_per_frame);
    REQUIRE(low.size() == quads_per_frame);
    const std::size_t tight = hedgr::ui::theme::intro_glow_layers - 1;
    /* The first quads are the widest: the last glow quad is the tight layer. */
    CHECK(low[tight].b / low[tight].r > 2.0f * (peak[tight].b / peak[tight].r));
    CHECK(low[tight].r < 0.45f);
    CHECK(peak[tight].r > 0.8f);
    CHECK(peak[tight].g > 0.9f);
    /* The wide haze stays a purple at the top: less red than the core. */
    CHECK(peak[0].r < peak[tight].r - 0.05f);
    CHECK(peak[0].b > peak[0].g);
    /* The crisp swirl is the brightest thing: white-hot at the peak. */
    const auto& crisp_peak = peak.back();
    CHECK(crisp_peak.r > 0.8f);
    CHECK(crisp_peak.g > 0.9f);
    CHECK(crisp_peak.b > 0.95f);
}

TEST_CASE("the crisp swirl climbs from blue to white as the brightness rises",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto low = read_quad_colours(desk, intro_peak_ns * first_tenth / 10).back();
    const auto mid = read_quad_colours(desk, intro_peak_ns * 6 / 10).back();
    const auto peak = read_quad_colours(desk, intro_peak_ns).back();
    CHECK(low.b > 3.0f * low.r);
    CHECK(low.b < 0.35f);
    CHECK(low.r < mid.r);
    CHECK(mid.r < peak.r);
    CHECK(low.b < mid.b);
    CHECK(mid.b < peak.b);
    CHECK(mid.b / mid.r < low.b / low.r);
    CHECK(peak.b / peak.r < mid.b / mid.r);
    /* Its alpha is held: only its colour carries the pulse. */
    CHECK(low.a == Catch::Approx(peak.a).margin(0.005));
}

TEST_CASE("the scene under the sign does not pulse: the vignette and fill are constant",
    "[intro][vignette]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto under = [&](std::int64_t t) {
        ImGui::NewFrame();
        draw_intro(desk.input(), logo, t);
        std::vector<ImDrawVert> out(
            ImGui::GetForegroundDrawList()->VtxBuffer.Data,
            ImGui::GetForegroundDrawList()->VtxBuffer.Data + under_text_vertices);
        ImGui::EndFrame();
        return out;
    };
    const auto reference = under(0);
    for (const std::int64_t t : {intro_peak_ns / 3, intro_peak_ns,
             (intro_peak_ns + intro_dark_ns) / 2}) {
        const auto frame = under(t);
        for (std::size_t i = 0; i < frame.size(); ++i) {
            CHECK(frame[i].col == reference[i].col);
            CHECK(frame[i].pos.x == reference[i].pos.x);
            CHECK(frame[i].pos.y == reference[i].pos.y);
        }
    }
}

TEST_CASE("the swirl is the configured fraction of the viewport's shorter side",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto quads = read_logo_quads(desk, intro_peak_ns);
    REQUIRE(quads.size() == quads_per_frame);
    CHECK(hedgr::ui::theme::intro_logo_fraction == Catch::Approx(0.20f));
    CHECK(quads.back().width == Catch::Approx(0.20f * 800.0f).epsilon(0.001));
    CHECK(quads.back().height == Catch::Approx(quads.back().width).epsilon(0.001));
}

TEST_CASE("the lockup sits centred, a little above the middle, the word below the swirl",
    "[intro][logo]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;

    const auto quads = read_logo_quads(desk, intro_peak_ns);
    const auto word = read_word(desk, intro_peak_ns);
    REQUIRE(quads.size() == quads_per_frame);
    const auto& swirl = quads.back();
    CHECK(swirl.centre_x == Catch::Approx(640.0f).margin(0.5));
    const float word_mid = (word.crisp_min_y + word.crisp_max_y) * 0.5f;
    const float lockup_mid = (swirl.centre_y - swirl.height * 0.5f
        + word.crisp_max_y) * 0.5f;
    CHECK(word.crisp_min_y > swirl.centre_y + swirl.height * 0.5f);
    CHECK(word_mid > swirl.centre_y);
    CHECK(lockup_mid < 400.0f);
    CHECK(lockup_mid > 400.0f - 0.12f * 800.0f);
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

namespace {

/** @brief The ORE swirl's document, as assets/branding/ore-logo-white.svg has it. */
constexpr std::string_view swirl_svg =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="35 35 824 824" width="256" height="256"><defs><path id="half" d="M112,690 A412,412 0 0 1 655,92 C700,140 712,200 705,250 C695,330 665,380 628,410 A183,183 0 0 0 268,395 C180,420 120,540 112,690 Z"/></defs><g fill="#fff"><use href="#half"/><use href="#half" transform="rotate(180 447 447)"/></g></svg>)svg";

using hedgr::ui::image::rgba_image;

std::uint8_t alpha_at(const rgba_image& image, int x, int y) {
    return image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4 + 3];
}

double total_alpha(const rgba_image& image) {
    double sum = 0.0;
    for (std::size_t i = 3; i < image.pixels.size(); i += 4)
        sum += image.pixels[i];
    return sum;
}

/** @brief The mean alpha of the 40 by 40 block whose corner is at @p x0, @p y0. */
double mean_alpha(const rgba_image& image, int x0, int y0) {
    double sum = 0.0;
    for (int y = y0; y < y0 + 40; ++y)
        for (int x = x0; x < x0 + 40; ++x)
            sum += alpha_at(image, x, y);
    return sum / 1600.0;
}

int steepest_step(const rgba_image& image) {
    int biggest = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x + 1 < image.width; ++x)
            biggest = std::max(biggest,
                std::abs(alpha_at(image, x + 1, y) - alpha_at(image, x, y)));
    }
    for (int x = 0; x < image.width; ++x) {
        for (int y = 0; y + 1 < image.height; ++y)
            biggest = std::max(biggest,
                std::abs(alpha_at(image, x, y + 1) - alpha_at(image, x, y)));
    }
    return biggest;
}

}

TEST_CASE("the glow mask is a smooth blur of the swirl", "[intro][blur]") {
    namespace theme = hedgr::ui::theme;
    const auto sharp = hedgr::ui::image::rasterize_svg_glow(swirl_svg,
        theme::intro_glow_raster, theme::intro_glow_inner, 0.0f);
    REQUIRE(sharp.has_value());
    /* The unblurred silhouette has hard edges: the bound below must be able to fail. */
    REQUIRE(steepest_step(*sharp) > 150);

    for (const float sigma : theme::intro_glow_sigmas) {
        const auto mask = hedgr::ui::image::rasterize_svg_glow(swirl_svg,
            theme::intro_glow_raster, theme::intro_glow_inner, sigma);
        REQUIRE(mask.has_value());
        REQUIRE(mask->width == theme::intro_glow_raster);
        /* A unit step blurred by a gaussian climbs at most 0.4 / sigma per pixel. */
        const int bound = static_cast<int>(255.0f * 0.4f / sigma * 1.25f) + 2;
        CHECK(steepest_step(*mask) <= bound);
        /* Energy is kept: the swirl is well inside the canvas. */
        CHECK(total_alpha(*mask) == Catch::Approx(total_alpha(*sharp)).epsilon(0.01));
        /* The light is brightest in the swirl and gone at the canvas corner. */
        const int mid = theme::intro_glow_raster / 2;
        CHECK(mean_alpha(*mask, mid - 20, mid - 20) > mean_alpha(*mask, 0, 0) + 20.0);
        CHECK(alpha_at(*mask, 0, 0) == 0);
    }
}

TEST_CASE("a wider blur is a softer, further-reaching light", "[intro][blur]") {
    namespace theme = hedgr::ui::theme;
    const auto tight = hedgr::ui::image::rasterize_svg_glow(swirl_svg,
        theme::intro_glow_raster, theme::intro_glow_inner, theme::intro_glow_sigmas[0]);
    const auto wide = hedgr::ui::image::rasterize_svg_glow(swirl_svg,
        theme::intro_glow_raster, theme::intro_glow_inner, theme::intro_glow_sigmas[2]);
    REQUIRE(tight.has_value());
    REQUIRE(wide.has_value());
    CHECK(steepest_step(*wide) < steepest_step(*tight));
    /* A point 20 px outside the swirl's edge, on the canvas row through its centre. */
    const int mid = theme::intro_glow_raster / 2;
    const int outside = mid - theme::intro_glow_inner / 2 - 20;
    CHECK(alpha_at(*wide, outside, mid) > alpha_at(*tight, outside, mid));
}

TEST_CASE("an unusable document or canvas blurs to nothing", "[intro][blur]") {
    CHECK_FALSE(hedgr::ui::image::rasterize_svg_glow("not svg", 64, 32, 2.0f));
    CHECK_FALSE(hedgr::ui::image::rasterize_svg_glow(swirl_svg, 16, 32, 2.0f));
}

namespace {

/** @brief Rasterises SVGs for real and hands them to the software rasteriser. */
struct raster_textures final : hedgr::ui::render::image_texture_source {
    explicit raster_textures(hedgr::testing::software_raster& target)
        : target_(target) {}

    ImTextureID get(std::string_view, std::string_view, std::string_view,
        const std::vector<std::uint8_t>&) override {
        return 0;
    }

    ImTextureID svg(std::string_view id, std::string_view document, int width,
        int height) override {
        return remember(std::format("{}@{}x{}", id, width, height), [&] {
            return hedgr::ui::image::rasterize_svg(document, width, height);
        });
    }

    ImTextureID svg_glow(std::string_view id, std::string_view document,
        int size, int inner, float sigma) override {
        return remember(std::format("{}@glow{}/{}x{}", id, size, inner, sigma), [&] {
            return hedgr::ui::image::rasterize_svg_glow(document, size, inner, sigma);
        });
    }

private:
    template<class Make>
    ImTextureID remember(const std::string& key, Make&& make) {
        if (const auto found = ids_.find(key); found != ids_.end())
            return static_cast<ImTextureID>(found->second);
        auto image = make();
        if (!image)
            return 0;
        const auto id = target_.add_texture(image->width, image->height,
            std::move(image->pixels));
        ids_.emplace(key, id);
        return static_cast<ImTextureID>(id);
    }

    hedgr::testing::software_raster& target_;
    std::map<std::string, std::uint64_t> ids_;
};

}

/*
 * Hidden from the suite: it writes the intro's frames for a person to look at.
 * Run it by name with HEDGR_INTRO_PPM set to a directory; it writes one PPM per
 * sample time. The wallpaper is left out, so the scene is black under the
 * vignette.
 */
TEST_CASE("intro capture", "[.gallery]") {
    const char* out = std::getenv("HEDGR_INTRO_PPM");
    REQUIRE(out != nullptr);
    hedgr::testing::headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    hedgr::testing::desk desk;
    hedgr::testing::software_raster raster(1280, 800);
    raster_textures textures(raster);
    auto input = desk.input();
    input.textures = &textures;
    std::string svg(swirl_svg);
    const std::pair<const char*, double> samples[] = {{"t0.0", 0.0},
        {"t1.0", 1.0}, {"t2.0", 2.0}, {"t3.023", 3.023}, {"t3.6", 3.6},
        {"t4.6", 4.6}, {"t5.4", 5.4}};
    for (const auto& [name, seconds] : samples) {
        for (int pass = 0; pass < 3; ++pass) {
            ImGui::NewFrame();
            draw_intro(input, svg, static_cast<std::int64_t>(seconds * 1e9));
            ImGui::Render();
            raster.update_textures();
        }
        raster.draw(*ImGui::GetDrawData(), IM_COL32(0, 0, 0, 255));
        CHECK(raster.write_ppm(std::filesystem::path(out) / (std::string("intro-") + name + ".ppm")));
    }
}
