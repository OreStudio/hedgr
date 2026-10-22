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
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "software_raster.hpp"
#include <catch2/catch_all.hpp>
#include <string_view>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;
using hedgr::testing::software_raster;

constexpr int display_width = 1280;
constexpr int display_height = 800;

/*
 * Two of the eight-bit steps the gradients are tuned in, summed over the three
 * channels: enough that a flat fill or a rounding error cannot pass.
 */
constexpr float perceptible = 6.0f / 255.0f;

struct themed final {
    headless_imgui imgui;
    themed() { theme::apply_theme(); }
};

float brightness(const ImVec4& c) {
    return c.x + c.y + c.z;
}

/** @brief Runs @p draw for a few frames and paints the last one. */
template<class Draw>
software_raster paint(Draw&& draw) {
    software_raster raster(display_width, display_height);
    for (int i = 0; i < 3; ++i) {
        draw();
        raster.update_textures();
    }
    raster.draw(*ImGui::GetDrawData(),
        ImGui::ColorConvertFloat4ToU32(theme::base_surface()));
    return raster;
}

/** @brief The screen bounds of the item a test drew. */
struct item_rect final {
    ImVec2 min;
    ImVec2 max;
};

}

TEST_CASE("a workspace window is lighter at its top than at its foot",
    "[gradient][workspace]") {
    themed context;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"}));
    render::workspace workspace;
    auto input = desk.input();
    const auto raster = paint([&] {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    });

    const ImGuiWindow* window = nullptr;
    for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if ((w->Flags & ImGuiWindowFlags_ChildWindow) == 0 && w->Active
            && std::string_view(w->Name).ends_with("###w"))
            window = w;
    }
    REQUIRE(window != nullptr);
    REQUIRE_FALSE(window->Collapsed);

    const int x = static_cast<int>(window->Pos.x) + 4;
    const int top = static_cast<int>(window->InnerRect.Min.y) + 2;
    const int foot = static_cast<int>(window->Pos.y + window->Size.y) - 10;
    const ImVec4 upper = raster.pixel(x, top);
    const ImVec4 lower = raster.pixel(x, foot);
    INFO("top " << brightness(upper) << " foot " << brightness(lower));
    CHECK(brightness(upper) > brightness(lower) + perceptible);

    const ImVec4 corner = raster.pixel(static_cast<int>(window->Pos.x),
        static_cast<int>(window->Pos.y + window->Size.y) - 1);
    const ImVec4 page = theme::base_surface();
    INFO("the rounded foot corner shows the page, not the fill");
    CHECK(brightness(corner) == Catch::Approx(brightness(page)).margin(0.02));
}

TEST_CASE("a square window, as ImGui draws one in a torn-out viewport, keeps "
    "its gradient and fills its corner", "[gradient][workspace][viewport]") {
    themed context;
    ImGui::GetStyle().WindowRounding = 0.0f;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"}));
    render::workspace workspace;
    auto input = desk.input();
    const auto raster = paint([&] {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    });

    const ImGuiWindow* window = nullptr;
    for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if ((w->Flags & ImGuiWindowFlags_ChildWindow) == 0 && w->Active
            && std::string_view(w->Name).ends_with("###w"))
            window = w;
    }
    REQUIRE(window != nullptr);
    REQUIRE_FALSE(window->Collapsed);

    const int x = static_cast<int>(window->Pos.x) + 4;
    const int top = static_cast<int>(window->InnerRect.Min.y) + 2;
    const int foot = static_cast<int>(window->Pos.y + window->Size.y) - 10;
    CHECK(brightness(raster.pixel(x, top))
        > brightness(raster.pixel(x, foot)) + perceptible);

    const ImVec4 corner = raster.pixel(static_cast<int>(window->Pos.x) + 1,
        static_cast<int>(window->Pos.y + window->Size.y) - 2);
    const ImVec4 page = theme::base_surface();
    INFO("a square foot corner is the fill, so the page does not show");
    CHECK(brightness(corner) != Catch::Approx(brightness(page)).margin(0.02));
}

TEST_CASE("a section card is lighter at its top than at its foot",
    "[gradient][card]") {
    themed context;
    item_rect card;
    const auto raster = paint([&] {
        hedgr::testing::frame([&] {
            if (widget::begin_section_card("card", 300.0f))
                ImGui::Dummy(ImVec2(100.0f, 160.0f));
            widget::end_section_card();
            card = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        });
    });

    const int x = static_cast<int>((card.min.x + card.max.x) * 0.5f);
    const ImVec4 upper = raster.pixel(x, static_cast<int>(card.min.y) + 3);
    const ImVec4 lower = raster.pixel(x, static_cast<int>(card.max.y) - 4);
    INFO("top " << brightness(upper) << " foot " << brightness(lower));
    CHECK(brightness(upper) > brightness(lower) + perceptible);
}

TEST_CASE("a good hero strip glows green at its left and fades to its right",
    "[gradient][hero]") {
    themed context;
    item_rect strip;
    const auto raster = paint([&] {
        hedgr::testing::frame([&] {
            widget::hero_strip("Current P/L", "+0.59%", theme::tone::good);
            strip = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        });
    });

    const int y = static_cast<int>(strip.min.y) + 3;
    const ImVec4 left = raster.pixel(static_cast<int>(strip.min.x) + 8, y);
    const ImVec4 right = raster.pixel(static_cast<int>(strip.max.x) - 8, y);
    const auto green_lead = [](const ImVec4& c) { return c.y - c.x; };
    INFO("left " << green_lead(left) << " right " << green_lead(right));
    CHECK(green_lead(left) > green_lead(right) + perceptible);
}
