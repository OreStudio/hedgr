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
#include "headless_desk.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <catch2/catch_all.hpp>
#include <vector>

namespace {

namespace theme = hedgr::ui::theme;
using hedgr::testing::headless_imgui;

/** @brief Three tabs in a themed headless window, the first of them chosen. */
struct tab_strip final {
    headless_imgui imgui;
    std::array<ImRect, 3> rects{};

    tab_strip() {
        theme::apply_theme();
    }

    /** @brief Opens the bar as a dock node does while its window is unfocused. */
    bool begin_bar(bool focused) {
        if (focused)
            return ImGui::BeginTabBar("##tabs",
                ImGuiTabBarFlags_DrawSelectedOverline);
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiWindow* window = g.CurrentWindow;
        ImGuiTabBar* bar = g.TabBars.GetOrAddByKey(window->GetID("##tabs"));
        bar->ID = window->GetID("##tabs");
        const ImVec2 pos = window->DC.CursorPos;
        return ImGui::BeginTabBarEx(bar,
            ImRect(pos.x, pos.y, window->WorkRect.Max.x,
                pos.y + ImGui::GetFrameHeight()),
            ImGuiTabBarFlags_DrawSelectedOverline);
    }

    void frame(bool unfocused = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 300.0f));
        ImGui::Begin("frame");
        if (begin_bar(!unfocused)) {
            const char* const labels[] = {"One", "Two", "Three"};
            for (std::size_t i = 0; i < 3; ++i) {
                if (ImGui::BeginTabItem(labels[i]))
                    ImGui::EndTabItem();
                rects[i] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            }
            ImGui::EndTabBar();
        }
        /* What the desk does to the dock nodes' tabs after the dock space. */
        for (const ImRect& box : rects) {
            theme::shade_tab(ImGui::GetWindowDrawList(), 0, box.Min,
                ImVec2(box.Max.x,
                    box.Max.y - ImGui::GetStyle().TabBarBorderSize));
        }
        ImGui::End();
        ImGui::Render();
    }

    void settle(bool unfocused = false) {
        for (int i = 0; i < 4; ++i)
            frame(unfocused);
    }

    void hover(std::size_t tab) {
        const ImVec2 at((rects[tab].Min.x + rects[tab].Max.x) * 0.5f,
            (rects[tab].Min.y + rects[tab].Max.y) * 0.5f);
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        settle();
    }

    [[nodiscard]] std::vector<ImDrawVert> vertices() const {
        const auto& buffer = ImGui::FindWindowByName("frame")->DrawList->VtxBuffer;
        return {buffer.begin(), buffer.end()};
    }

    /**
     * @brief The vertices of tab @p tab's shape. With @p fringe the antialiased
     * rim and the label's glyph quads are in too; without it only the solid
     * interior of the fill.
     */
    [[nodiscard]] std::vector<ImDrawVert> vertices_in(std::size_t tab,
        bool fringe) const {
        const ImRect box = rects[tab];
        std::vector<ImDrawVert> out;
        const float foot = box.Max.y - ImGui::GetStyle().TabBarBorderSize;
        for (const ImDrawVert& v : vertices()) {
            if (v.pos.x < box.Min.x - 1.0f || v.pos.x > box.Max.x + 1.0f
                || v.pos.y < box.Min.y - 1.0f || v.pos.y > foot + 0.5f)
                continue;
            if (!fringe && ((v.col >> IM_COL32_A_SHIFT) != 255u
                    || v.col == ImGui::ColorConvertFloat4ToU32(theme::tab_label_colour())
                    || v.pos.y < box.Min.y || v.pos.y > foot))
                continue;
            out.push_back(v);
        }
        return out;
    }

    /** @brief Whether a vertex of @p col lies within tab @p tab's rectangle. */
    [[nodiscard]] bool filled(std::size_t tab, ImU32 col) const {
        const ImRect box = rects[tab];
        const auto all = vertices();
        return std::ranges::any_of(all, [&](const ImDrawVert& v) {
            return v.col == col && v.pos.x >= box.Min.x - 1.0f
                && v.pos.x <= box.Max.x + 1.0f && v.pos.y >= box.Min.y - 1.0f
                && v.pos.y <= box.Max.y + 1.0f;
        });
    }
};

ImU32 u32(ImVec4 value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

}

TEST_CASE("the chosen tab is the active fill, the others the idle fill",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    /* The reference's sampled fills: Statistics tab and Dear ImGui Demo tab. */
    CHECK(u32(theme::tab_active_colour()) == IM_COL32(0x73, 0x60, 0x8d, 255));
    CHECK(u32(theme::tab_idle_colour()) == IM_COL32(0x2b, 0x2d, 0x38, 255));
    CHECK(strip.filled(0, u32(theme::tab_active_colour())));
    CHECK_FALSE(strip.filled(0, u32(theme::tab_idle_colour())));
    CHECK(strip.filled(1, u32(theme::tab_idle_colour())));
    CHECK_FALSE(strip.filled(1, u32(theme::tab_active_colour())));
}

TEST_CASE("a tab under the pointer takes the hover tint", "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    strip.hover(2);
    CHECK(strip.filled(2, u32(theme::tab_hover_colour())));
    CHECK_FALSE(strip.filled(1, u32(theme::tab_hover_colour())));
}

TEST_CASE("a tab's top corners are rounded and its bottom ones are not",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    CHECK(ImGui::GetStyle().TabRounding == theme::tab_rounding);
    const ImRect box = strip.rects[1];
    const auto nearest = [&](float x, float y) {
        float best = 1.0e9f;
        for (const ImDrawVert& v : strip.vertices_in(1, true)) {
            best = std::min(best, std::hypot(v.pos.x - x, v.pos.y - y));
        }
        return best;
    };
    /* A square corner has a vertex on it; a rounded one is cut back. */
    CHECK(nearest(box.Min.x, box.Min.y) > 1.5f);
    CHECK(nearest(box.Max.x, box.Min.y) > 1.5f);
    CHECK(nearest(box.Min.x, box.Max.y - ImGui::GetStyle().TabBarBorderSize)
        < 1.0f);
}

namespace {

/** @brief A colour's lightness, enough to order two shades of one hue. */
int light(ImU32 col) {
    return static_cast<int>((col >> IM_COL32_R_SHIFT) & 0xffu)
        + static_cast<int>((col >> IM_COL32_G_SHIFT) & 0xffu)
        + static_cast<int>((col >> IM_COL32_B_SHIFT) & 0xffu);
}

/**
 * @brief Checks tab @p tab fades from @p top at its head to @p bottom at its
 * foot, never darker above than below.
 */
void check_fade(const tab_strip& strip, std::size_t tab, ImU32 top,
    ImU32 bottom) {
    auto solid = strip.vertices_in(tab, false);
    /* The label's glyph quads share the box and are no part of the fill. */
    std::erase_if(solid, [&](const ImDrawVert& v) {
        return light(v.col) > light(top) || light(v.col) < light(bottom);
    });
    REQUIRE_FALSE(solid.empty());
    std::ranges::sort(solid, {}, [](const ImDrawVert& v) { return v.pos.y; });
    CHECK(solid.front().col == top);
    CHECK(solid.back().col == bottom);
    CHECK(light(top) > light(bottom));
    for (std::size_t i = 1; i < solid.size(); ++i) {
        if (solid[i].pos.y > solid[i - 1].pos.y)
            CHECK(light(solid[i].col) <= light(solid[i - 1].col));
    }
}

}

TEST_CASE("the chosen tab fades from a lighter head to the sampled fill",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    check_fade(strip, 0, theme::tab_active_top,
        u32(theme::tab_active_colour()));
}

TEST_CASE("an idle tab fades from a lighter head to the sampled fill",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    check_fade(strip, 1, theme::tab_idle_top, u32(theme::tab_idle_colour()));
}

TEST_CASE("a hovered tab fades from a lighter head to its hover fill",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    strip.hover(2);
    check_fade(strip, 2, theme::tab_hover_top, u32(theme::tab_hover_colour()));
}

TEST_CASE("a tab's head is about twelve points lighter than its foot, not a "
          "different colour",
    "[look][tabs]") {
    for (const auto& [top, bottom] : {
             std::pair{theme::tab_active_top, u32(theme::tab_active_colour())},
             std::pair{theme::tab_idle_top, u32(theme::tab_idle_colour())},
             std::pair{theme::tab_hover_top, u32(theme::tab_hover_colour())}}) {
        const int lift = light(top) - light(bottom);
        CHECK(lift >= 3 * 20);
        CHECK(lift <= 3 * 45);
    }
}

TEST_CASE("a tab has no outline and the chosen one no overline", "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    CHECK(ImGui::GetStyle().TabBorderSize == 0.0f);
    CHECK(ImGui::GetStyle().TabBarOverlineSize == 0.0f);
    CHECK_FALSE(strip.filled(0, u32(theme::chart_line_colour())));
    /* The widget tabs still draw their underline from this colour. */
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_TabSelectedOverline].x
        == theme::chart_line_colour().x);
}

TEST_CASE("the rule under the strip is the chosen tab's colour and runs past "
          "the last tab",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    CHECK(ImGui::GetStyle().TabBarBorderSize == 1.0f);
    const ImU32 active = u32(theme::tab_active_colour());
    const float past = strip.rects[2].Max.x + 20.0f;
    const auto all = strip.vertices();
    CHECK(std::ranges::any_of(all, [&](const ImDrawVert& v) {
        return v.col == active && v.pos.x >= past;
    }));
}

TEST_CASE("a strip that lost focus is drawn as the focused one, as the "
          "reference draws its strips",
    "[look][tabs]") {
    tab_strip strip;
    strip.settle(true);
    const ImGuiStyle& style = ImGui::GetStyle();
    CHECK(u32(style.Colors[ImGuiCol_TabDimmedSelected])
        == u32(style.Colors[ImGuiCol_TabSelected]));
    CHECK(u32(style.Colors[ImGuiCol_TabDimmed]) == u32(style.Colors[ImGuiCol_Tab]));
    CHECK(strip.filled(0, u32(theme::tab_active_colour())));
    CHECK(strip.filled(1, u32(theme::tab_idle_colour())));
}

TEST_CASE("the tabs of a dock node fade like the widget tabs", "[look][tabs][docking]") {
    headless_imgui imgui;
    theme::apply_theme();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool built = false;
    ImGuiID space = 0;
    const auto frame = [&](bool shade) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("desk", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoBackground);
        space = ImGui::GetID("space");
        if (!built) {
            ImGui::DockBuilderAddNode(space, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodeSize(space, ImVec2(600.0f, 400.0f));
            ImGui::DockBuilderDockWindow("One", space);
            ImGui::DockBuilderDockWindow("Two", space);
            ImGui::DockBuilderFinish(space);
            built = true;
        }
        ImGui::DockSpace(space);
        if (shade)
            theme::shade_dock_tabs();
        ImGui::End();
        for (const char* name : {"One", "Two"}) {
            ImGui::Begin(name);
            ImGui::End();
        }
        ImGui::Render();
    };
    for (int i = 0; i < 5; ++i)
        frame(true);
    /* The dock space's strip is drawn in the child window that hosts it. */
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(space);
    REQUIRE(node != nullptr);
    REQUIRE(node->HostWindow != nullptr);
    const auto& buffer = node->HostWindow->DrawList->VtxBuffer;
    const auto has = [&](ImU32 col) {
        return std::ranges::any_of(buffer, [&](const ImDrawVert& v) {
            return v.col == col;
        });
    };
    /* A chosen tab and an idle one, each from its head to its foot. */
    CHECK(has(theme::tab_active_top));
    CHECK(has(u32(theme::tab_active_colour())));
    CHECK(has(theme::tab_idle_top));
    CHECK(has(u32(theme::tab_idle_colour())));
}
