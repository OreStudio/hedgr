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
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/tabs.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::frame;
using hedgr::testing::headless_imgui;
using Catch::Approx;

constexpr std::array all_tones{theme::tone::neutral, theme::tone::good,
    theme::tone::bad, theme::tone::warning};

/** @brief The vertices @p draw adds to the current window's draw list. */
template<class Draw>
std::vector<ImDrawVert> vertices_of(Draw&& draw) {
    ImDrawList* const list = ImGui::GetWindowDrawList();
    const int first = list->VtxBuffer.Size;
    draw();
    return {list->VtxBuffer.begin() + first, list->VtxBuffer.end()};
}

bool has_colour(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    return std::ranges::any_of(vertices,
        [&](const ImDrawVert& v) { return v.col == colour; });
}

constexpr ImU32 rgb(ImU32 colour) {
    return colour & ~IM_COL32_A_MASK;
}

constexpr ImU32 alpha(ImU32 colour) {
    return (colour & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
}

/** @brief Whether any visible vertex is drawn in @p colour's hue. */
bool has_hue(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    return std::ranges::any_of(vertices, [&](const ImDrawVert& v) {
        return alpha(v.col) != 0 && rgb(v.col) == rgb(colour);
    });
}

/**
 * @brief Whether @p v belongs to a filled shape rather than to text or to an
 * antialiased line, whose quads carry transparent texels past the edge.
 */
bool is_solid(const ImDrawVert& v) {
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    return v.uv.x == white.x && v.uv.y == white.y;
}

ImU32 accent() {
    return ImGui::GetColorU32(ImGuiCol_CheckMark);
}

float max_x(const std::vector<ImDrawVert>& vertices) {
    float result = -FLT_MAX;
    for (const auto& v : vertices)
        result = std::max(result, v.pos.x);
    return result;
}

ImVec2 centre_of_item() {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
}

/** @brief A themed headless context, so the accent and the chip are real. */
struct themed final {
    headless_imgui imgui;
    themed() { theme::apply_theme(); }
};

}

TEST_CASE("a caption measures its capitals and the tracking between them",
    "[cells][caption]") {
    themed context;
    frame([] {
        const float tracking = widget::caption_tracking();
        CHECK(tracking > 0.0f);

        const float one = widget::caption_size("A").x;
        const float two = widget::caption_size("AA").x;
        const float three = widget::caption_size("AAA").x;
        const float step = two - one;
        CHECK(three - two == Approx(step));
        CHECK(one == Approx(step - tracking));
        CHECK(widget::caption_size("").x == 0.0f);

        CHECK(widget::caption_size("date").x
            == Approx(widget::caption_size("DATE").x));
        CHECK(widget::caption_size("DATE", "help").x
            > widget::caption_size("DATE").x);
    });
}

TEST_CASE("a caption records the size it measures and draws inside it",
    "[cells][caption]") {
    themed context;
    frame([] {
        widget::caption("Current P/L", "Profit since entry");
        const ImVec2 measured = widget::caption_size("Current P/L",
            "Profit since entry");
        CHECK(ImGui::GetItemRectSize().x == Approx(measured.x));
        CHECK(ImGui::GetItemRectSize().y == Approx(measured.y));

        ImGui::SameLine(0.0f, 0.0f);
        CHECK(ImGui::GetCursorScreenPos().x
            == Approx(ImGui::GetItemRectMin().x + measured.x));
        ImGui::NewLine();

        constexpr const char* text = "AAAAAAAAAA";
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = widget::caption_size(text).x;
        const float step = widget::caption_size("AA").x
            - widget::caption_size("A").x;
        const auto vertices = vertices_of([&] { widget::caption(text); });
        CHECK(vertices.size() == 4u * 10u);
        CHECK(max_x(vertices) <= origin.x + width + 1.0f);
        CHECK(max_x(vertices) >= origin.x + width - step);
    });
}

TEST_CASE("a caption draws its help ring only when it has help",
    "[cells][caption][help]") {
    themed context;
    frame([] {
        const ImU32 ring = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const auto plain = vertices_of([] { widget::caption("Points"); });
        CHECK_FALSE(has_colour(plain, ring));

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const auto helped = vertices_of(
            [] { widget::caption("Points", "Observations plotted."); });
        CHECK(has_colour(helped, ring));
        const float text_right = origin.x + widget::caption_size("Points").x;
        for (const auto& v : helped) {
            if (v.col == ring)
                CHECK(v.pos.x > text_right);
        }

        const auto bare_cell = vertices_of(
            [] { widget::labelled_value("Date", "15 Oct", 160.0f); });
        CHECK_FALSE(has_colour(bare_cell, ring));
        const auto helped_cell = vertices_of(
            [] { widget::labelled_value("Date", "15 Oct", 160.0f, "Opened"); });
        CHECK(has_colour(helped_cell, ring));
    });
}

TEST_CASE("a hovered caption lights its help ring", "[cells][caption][help]") {
    themed context;
    const ImU32 resting = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 lit = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 caption_centre;
    ImVec2 cell_centre;
    std::vector<ImDrawVert> caption;
    std::vector<ImDrawVert> cell;
    const auto draw = [&] {
        frame([&] {
            caption = vertices_of([] { widget::caption("Terms", "Tenors."); });
            caption_centre = centre_of_item();
            cell = vertices_of(
                [] { widget::labelled_value("Date", "15 Oct", 160.0f, "Opened"); });
            cell_centre = centre_of_item();
        });
    };
    draw();
    CHECK(has_colour(caption, resting));
    CHECK_FALSE(has_colour(caption, lit));

    ImGui::GetIO().AddMousePosEvent(caption_centre.x, caption_centre.y);
    draw();
    draw();
    CHECK(has_colour(caption, lit));
    CHECK_FALSE(has_colour(caption, resting));
    CHECK(has_colour(cell, resting));

    ImGui::GetIO().AddMousePosEvent(cell_centre.x, cell_centre.y);
    draw();
    draw();
    CHECK(has_colour(cell, lit));
    CHECK_FALSE(has_colour(cell, resting));
    CHECK(has_colour(caption, resting));
}

TEST_CASE("the semantic tones take the state colours and never the accent",
    "[cells][tone]") {
    themed context;
    frame([] {
        CHECK(theme::pill_colours(theme::tone::good).text == theme::good_colour);
        CHECK(theme::pill_colours(theme::tone::bad).text
            == theme::failure_colour);
        CHECK(theme::pill_colours(theme::tone::warning).text
            == theme::warning_colour);

        const auto neutral = theme::pill_colours(theme::tone::neutral);
        CHECK(neutral.text == ImGui::GetColorU32(ImGuiCol_Text));
        CHECK(neutral.fill == ImGui::GetColorU32(ImGuiCol_FrameBg));

        for (const auto tone : {theme::tone::good, theme::tone::bad,
                 theme::tone::warning}) {
            const auto pill = theme::pill_colours(tone);
            const auto band = theme::band_colours(tone);
            CHECK(rgb(pill.fill) == rgb(pill.text));
            CHECK(rgb(pill.border) == rgb(pill.text));
            CHECK(alpha(pill.fill) < alpha(pill.border));
            CHECK(alpha(pill.border) < 255u);
            CHECK(band.text == pill.text);
            CHECK(alpha(band.fill) < alpha(pill.fill));
        }

        for (const auto tone : all_tones) {
            for (const auto colours :
                {theme::pill_colours(tone), theme::band_colours(tone)}) {
                CHECK(rgb(colours.fill) != rgb(accent()));
                CHECK(rgb(colours.border) != rgb(accent()));
                CHECK(rgb(colours.text) != rgb(accent()));
            }
        }
    });
}

TEST_CASE("a value pill is drawn in its tone's colours at its measured size",
    "[cells][pill]") {
    themed context;
    frame([] {
        for (const auto tone : all_tones) {
            const auto colours = theme::pill_colours(tone);
            const auto vertices =
                vertices_of([&] { widget::value_pill("SHORT", tone); });
            CHECK(has_colour(vertices, colours.fill));
            CHECK(has_colour(vertices, colours.border));
            CHECK(has_colour(vertices, colours.text));
            CHECK_FALSE(has_hue(vertices, accent()));

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            float text_left = FLT_MAX;
            float text_right = -FLT_MAX;
            for (const auto& v : vertices) {
                if (is_solid(v) || v.col != colours.text)
                    continue;
                text_left = std::min(text_left, v.pos.x);
                text_right = std::max(text_right, v.pos.x);
            }
            REQUIRE(text_left < text_right);
            CHECK(text_left > min.x);
            CHECK(text_right < max.x);
            CHECK((text_left - min.x) == Approx(max.x - text_right).margin(2.0));

            const ImVec2 size = widget::value_pill_size("SHORT");
            CHECK(ImGui::GetItemRectSize().x == Approx(size.x));
            CHECK(ImGui::GetItemRectSize().y == Approx(size.y));
        }
        CHECK(widget::value_pill_size("8-HOUR").x
            > widget::value_pill_size("8").x);
        CHECK(widget::value_pill_size("8-HOUR").y
            == Approx(widget::value_pill_size("8").y));
    });
}

TEST_CASE("a pill and a caption sit in the middle of a taller line",
    "[cells][pill]") {
    themed context;
    frame([] {
        const float fresh_top = ImGui::GetCursorScreenPos().y;
        widget::value_pill("FRESH");
        CHECK(ImGui::GetItemRectMin().y == Approx(std::floor(fresh_top)));

        ImGui::AlignTextToFramePadding();
        const float top = ImGui::GetCursorScreenPos().y;
        const float line = ImGui::GetFrameHeight();
        widget::value_pill("LIVE");
        const float pill = widget::value_pill_size("LIVE").y;
        CHECK(ImGui::GetItemRectMin().y
            == Approx(std::floor(top) + std::floor((line - pill) * 0.5f)));
        CHECK(ImGui::GetItemRectSize().y == Approx(pill));

        ImGui::SameLine();
        widget::caption("Points");
        const float caption = widget::caption_size("Points").y;
        CHECK(ImGui::GetItemRectMin().y
            == Approx(std::floor(top) + std::floor((line - caption) * 0.5f)));

        ImGui::SameLine();
        ImGui::Button("next");
        CHECK(ImGui::GetItemRectMin().y == Approx(top));
    });
}

TEST_CASE("a caption and a pill hand their text to ImGui's log",
    "[cells][pill]") {
    themed context;
    std::string text;
    frame([&] {
        ImGui::LogToBuffer();
        widget::caption("Type");
        ImGui::SameLine();
        widget::value_pill("SHORT", theme::tone::bad);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });
    CHECK(text.find("Type SHORT") != std::string::npos);
}

TEST_CASE("labelled cells of one width form aligned columns",
    "[cells][labelled]") {
    themed context;
    frame([] {
        constexpr float width = 160.0f;
        const float height = widget::labelled_value_height();
        CHECK(height >= widget::caption_size("X", "help").y
            + widget::value_pill_size("X").y);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        widget::labelled_value("Date", "15 Oct 2026", width, "When it opened");
        CHECK(ImGui::GetItemRectSize().x == Approx(width));
        CHECK(ImGui::GetItemRectSize().y == Approx(height));

        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("Type", "SHORT", theme::tone::bad, width, "Side");
        CHECK(ImGui::GetItemRectMin().x == Approx(origin.x + width));
        CHECK(ImGui::GetItemRectMin().y == Approx(origin.y));
        CHECK(ImGui::GetItemRectSize().y == Approx(height));

        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("TF", "8-HOUR", theme::tone::neutral, width);
        CHECK(ImGui::GetItemRectMin().x == Approx(origin.x + 2.0f * width));
        CHECK(ImGui::GetItemRectMax().y == Approx(origin.y + height));

        const auto stays_inside = [&](auto&& draw) {
            const ImVec2 top = ImGui::GetCursorScreenPos();
            const auto cell = vertices_of(draw);
            REQUIRE_FALSE(cell.empty());
            for (const auto& v : cell) {
                if (!is_solid(v) || alpha(v.col) == 0)
                    continue;
                CHECK(v.pos.y >= top.y);
                CHECK(v.pos.y <= top.y + height);
            }
        };
        for (const auto tone : all_tones) {
            stays_inside([&] {
                widget::labelled_pill("Power", "STRONG", tone, width, "Help");
            });
        }
        stays_inside([&] {
            widget::labelled_value("Date", "15 Oct 2026", width, "Help");
        });

        const ImVec2 narrow = ImGui::GetCursorScreenPos();
        const auto vertices = vertices_of([&] {
            widget::labelled_value("Category", "A value far wider than it",
                40.0f);
        });
        CHECK(max_x(vertices) <= narrow.x + 40.0f);
    });
}

TEST_CASE("a hero strip spans the width and wears its tone",
    "[cells][hero]") {
    themed context;
    frame([] {
        const float available = ImGui::GetContentRegionAvail().x;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const auto good = vertices_of([] {
            widget::hero_strip("Current P/L", "+0.59%", theme::tone::good,
                "TARGETING TP2", "Profit since entry");
        });
        CHECK(ImGui::GetItemRectSize().x == Approx(available));
        CHECK(ImGui::GetItemRectSize().y
            == Approx(widget::hero_strip_height()));
        CHECK(widget::hero_strip_height() > widget::value_pill_size("X").y);
        CHECK(has_hue(good, theme::band_fade(theme::tone::good).from));
        CHECK(has_colour(good, theme::pill_colours(theme::tone::good).fill));
        CHECK(has_colour(good, theme::good_colour));
        CHECK_FALSE(has_hue(good, theme::failure_colour));
        CHECK_FALSE(has_hue(good, accent()));
        CHECK(max_x(good) <= origin.x + available + 1.0f);

        const auto bad = vertices_of([] {
            widget::hero_strip("Current P/L", "-1.20%", theme::tone::bad);
        });
        CHECK(has_hue(bad, theme::band_fade(theme::tone::bad).from));
        CHECK(has_colour(bad, theme::failure_colour));
        CHECK_FALSE(has_hue(bad, theme::good_colour));
        CHECK_FALSE(has_hue(bad, accent()));
    });
}

TEST_CASE("a section card grows to its contents and divides its header",
    "[cells][card]") {
    themed context;
    ImVec2 size;
    std::vector<ImDrawVert> divider;
    float interior = 0.0f;
    for (int i = 0; i < 2; ++i) {
        frame([&] {
            if (widget::begin_section_card("card", 300.0f)) {
                interior = ImGui::GetContentRegionAvail().x;
                divider = vertices_of([] { widget::section_card_divider(); });
                ImGui::Dummy(ImVec2(100.0f, 50.0f));
            }
            widget::end_section_card();
            size = ImGui::GetItemRectSize();
        });
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    CHECK(size.x == Approx(300.0f));
    CHECK(size.y == Approx(1.0f + spacing + 50.0f
        + 2.0f * widget::section_card_padding));
    CHECK(interior == Approx(300.0f - 2.0f * widget::section_card_padding));
    REQUIRE_FALSE(divider.empty());
    CHECK(has_colour(divider, ImGui::GetColorU32(ImGuiCol_Separator)));
}

TEST_CASE("the chosen tab is underlined in the accent", "[cells][tabs]") {
    themed context;
    frame([] {
        REQUIRE(ImGui::BeginTabBar("tabs"));
        if (ImGui::BeginTabItem("Tracked")) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const auto line = vertices_of([] { widget::underline_selected_tab(); });
            REQUIRE_FALSE(line.empty());
            for (const auto& v : line) {
                CHECK(v.col == accent());
                CHECK(v.pos.x >= min.x);
                CHECK(v.pos.x <= max.x);
                CHECK(v.pos.y >= max.y - widget::tab_underline_thickness);
                CHECK(v.pos.y <= max.y);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    });
}

TEST_CASE("a steady frame of 200 cells allocates nothing", "[cells][frame]") {
    themed context;
    ImGui::GetIO().DisplaySize = ImVec2(1600.0f, 1400.0f);
    constexpr int columns = 8;
    constexpr int cells = 200;
    int visible = 0;
    const auto draw = [&] {
        visible = 0;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1600.0f, 1400.0f));
        ImGui::Begin("cells");
        widget::hero_strip("Current P/L", "+0.59%", theme::tone::good,
            "TARGETING TP2", "Profit since entry");
        for (int i = 0; i < cells; ++i) {
            if (i % columns != 0)
                ImGui::SameLine(0.0f, 0.0f);
            if (i % 2 == 0)
                widget::labelled_value("Date", "15 Oct 2026", 180.0f, "Opened");
            else
                widget::labelled_pill("Type", "SHORT",
                    all_tones[static_cast<std::size_t>(i / 2) % all_tones.size()],
                    180.0f, "Side");
            if (ImGui::IsItemVisible())
                ++visible;
        }
        ImGui::End();
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    const auto allocations = hedgr::testing::allocations() - before;
    CHECK(visible == cells);
    CHECK(allocations == 0u);
}
