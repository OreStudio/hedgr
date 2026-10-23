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
    const ImU32 idle = u32(theme::tab_idle_colour());
    const auto nearest = [&](float x, float y) {
        float best = 1.0e9f;
        for (const ImDrawVert& v : strip.vertices()) {
            if (v.col == idle)
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

TEST_CASE("the chosen tab wears the chart purple overline", "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_TabSelectedOverline].x
        == theme::chart_line_colour().x);
    CHECK(strip.filled(0, u32(theme::chart_line_colour())));
    CHECK_FALSE(strip.filled(1, u32(theme::chart_line_colour())));
}

TEST_CASE("a tab bar whose window lost focus is dimmer", "[look][tabs]") {
    tab_strip strip;
    strip.settle();
    const ImU32 focused = u32(ImGui::GetStyle().Colors[ImGuiCol_TabSelected]);
    const ImU32 dimmed = u32(ImGui::GetStyle().Colors[ImGuiCol_TabDimmedSelected]);
    strip.settle(true);
    CHECK(strip.filled(0, dimmed));
    CHECK_FALSE(strip.filled(0, focused));
}
