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
#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <string>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;
using widget::menu_after_entry;
using widget::menu_bar_state;

constexpr const char* bar_window = "###hedgr-menu-bar";
constexpr const char* system_popup = "system menu";

/** @brief Whether the popup @p id, opened from window @p owner, is open. */
bool popup_open_in(const char* owner, const char* id) {
    ImGuiWindow* window = ImGui::FindWindowByName(owner);
    return window != nullptr
        && ImGui::IsPopupOpen(window->GetID(id), ImGuiPopupFlags_None);
}

ImU32 u32(ImVec4 value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

/** @brief The top bar drawn alone, over a desk, with the mouse under test control. */
struct bar_harness final {
    headless_imgui imgui;
    desk desk;
    render::render_input input;
    menu_bar_state state;
    render::menu_bar_result result;
    std::string svg = "<svg xmlns='http://www.w3.org/2000/svg'/>";

    bar_harness() : input(desk.input()) {
        theme::apply_theme();
    }

    void frame() {
        result = {};
        ImGui::NewFrame();
        render::draw_menu_bar(input, svg, state, result);
        ImGui::Render();
    }

    void settle() {
        for (int i = 0; i < 3; ++i)
            frame();
    }

    void hover(ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        frame();
    }

    void click(ImVec2 at) {
        hover(at);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    }

    [[nodiscard]] static ImVec2 logo_centre() {
        return ImVec2(theme::menu_bar_height * 0.5f,
            theme::menu_bar_height * 0.5f);
    }

    /** @brief The centre of the n-th row of the system menu, from the top. */
    [[nodiscard]] static ImVec2 row_centre(int n) {
        const float row = ImGui::GetTextLineHeight()
            + 2.0f * theme::menu_row_padding_y;
        return ImVec2(theme::menu_padding + theme::menu_min_width * 0.5f,
            theme::menu_bar_height + theme::menu_padding
                + (row + theme::menu_row_gap) * static_cast<float>(n) + row * 0.5f);
    }

    void open_menu() {
        settle();
        click(logo_centre());
        settle();
    }

    [[nodiscard]] static bool popup_open() {
        return popup_open_in(bar_window, system_popup);
    }

    [[nodiscard]] static const ImGuiWindow* bar() {
        return ImGui::FindWindowByName(bar_window);
    }
};

}

TEST_CASE("an entry press opens it and a second press closes it", "[menu_bar]") {
    CHECK(menu_after_entry(menu_bar_state::none, 0, false, true, true) == 0);
    CHECK(menu_after_entry(0, 0, true, true, true)
        == menu_bar_state::none);
}

TEST_CASE("the pointer on another entry moves an open menu there and does nothing "
          "while none is open",
    "[menu_bar]") {
    CHECK(menu_after_entry(0, 1, true, false, true) == 1);
    CHECK(menu_after_entry(0, 1, true, false, false) == 0);
    CHECK(menu_after_entry(menu_bar_state::none, 1, false, false, true)
        == menu_bar_state::none);
}

TEST_CASE("a press on an open entry whose popup ImGui already closed stays closed",
    "[menu_bar]") {
    CHECK(menu_after_entry(0, 0, false, true, true) == menu_bar_state::none);
    CHECK(menu_after_entry(0, 0, false, false, false) == menu_bar_state::none);
}

TEST_CASE("clearing the bar's open menu closes its popup", "[menu_bar]") {
    bar_harness h;
    h.open_menu();
    REQUIRE(bar_harness::popup_open());
    h.state.open = menu_bar_state::none;
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
}

TEST_CASE("the bar is a fixed-height window across the top of the viewport",
    "[menu_bar]") {
    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    CHECK(bar->Pos.x == Catch::Approx(viewport->Pos.x));
    CHECK(bar->Pos.y == Catch::Approx(viewport->Pos.y));
    CHECK(bar->Size.x == Catch::Approx(viewport->Size.x));
    CHECK(bar->Size.y == Catch::Approx(theme::menu_bar_height));
    CHECK((bar->Flags & ImGuiWindowFlags_NoMove) != 0);
    CHECK((bar->Flags & ImGuiWindowFlags_NoDocking) != 0);
}

TEST_CASE("the bar is flat dark with a hairline along its bottom edge",
    "[menu_bar][look]") {
    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    const auto& verts = bar->DrawList->VtxBuffer;
    const float bottom = bar->Pos.y + bar->Size.y;
    CHECK(std::ranges::any_of(verts, [&](const ImDrawVert& v) {
        return v.col == u32(theme::menu_bar_surface());
    }));
    CHECK(std::ranges::any_of(verts, [&](const ImDrawVert& v) {
        return v.col == u32(theme::menu_border()) && v.pos.y > bottom - 1.5f
            && v.pos.y <= bottom;
    }));
}

TEST_CASE("the logo is drawn at the bar's scale and lights while hovered",
    "[menu_bar][look]") {
    bar_harness h;
    h.settle();
    const float inner = theme::menu_bar_height - 2.0f * theme::menu_bar_logo_padding;
    CHECK(h.desk.textures.svg_gets > 0);
    CHECK(h.desk.textures.svg_width == static_cast<int>(std::lround(inner)));
    CHECK(h.desk.textures.svg_height == h.desk.textures.svg_width);

    const auto tinted = [&](ImU32 colour) {
        const auto& verts = h.bar()->DrawList->VtxBuffer;
        return std::ranges::any_of(verts, [&](const ImDrawVert& v) {
            return v.col == colour && v.uv.x >= 0.99f && v.uv.y >= 0.99f;
        });
    };
    CHECK(tinted(u32(theme::dim_text())));
    h.hover(bar_harness::logo_centre());
    h.settle();
    CHECK(tinted(ImGui::GetColorU32(ImGuiCol_Text)));
    CHECK_FALSE(tinted(u32(theme::dim_text())));
}

TEST_CASE("a bar with no logo document still has a working logo button",
    "[menu_bar]") {
    bar_harness h;
    h.svg.clear();
    h.settle();
    CHECK(h.desk.textures.svg_gets == 0);
    h.click(bar_harness::logo_centre());
    h.settle();
    CHECK(bar_harness::popup_open());
}

TEST_CASE("clicking the logo opens the system menu and clicking it again closes it",
    "[menu_bar]") {
    bar_harness h;
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    h.click(bar_harness::logo_centre());
    h.settle();
    CHECK(bar_harness::popup_open());
    CHECK(h.state.open == 0);
    h.click(bar_harness::logo_centre());
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    CHECK(h.state.open == menu_bar_state::none);
}

TEST_CASE("clicking away closes the system menu", "[menu_bar]") {
    bar_harness h;
    h.open_menu();
    REQUIRE(bar_harness::popup_open());
    h.click(ImVec2(900.0f, 600.0f));
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    CHECK(h.state.open == menu_bar_state::none);
}

TEST_CASE("the Instances row is first and asks for the Instances window",
    "[menu_bar]") {
    bar_harness h;
    h.open_menu();
    REQUIRE(bar_harness::popup_open());
    h.click(bar_harness::row_centre(0));
    CHECK(h.result.focus_instances);
    bool asked = false;
    for (auto& c : h.desk.commands.drain())
        asked = asked || (c.kind == model::command::command_kind::open_window
            && c.new_window_kind == model::domain::window_kind::instance_manager);
    CHECK(asked);
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
}

TEST_CASE("the next row locks an open store", "[menu_bar]") {
    bar_harness h;
    h.open_menu();
    bool locked = false;
    for (int i = 0; i < 6 && !locked; ++i) {
        h.frame();
        if (i == 0)
            h.click(bar_harness::row_centre(1));
        locked = locked || h.result.lock_requested;
    }
    CHECK(locked);
}

TEST_CASE("a locked store shows the way in and no Instances row",
    "[menu_bar][lock]") {
    bar_harness h;
    h.input.store_lock = model::domain::store_lock::locked;
    h.open_menu();
    REQUIRE(bar_harness::popup_open());
    h.click(bar_harness::row_centre(0));
    CHECK_FALSE(h.result.focus_instances);
    CHECK(h.result.unlock_requested);
    CHECK(h.desk.commands.drain().empty());
}

TEST_CASE("a store with no password offers to create one", "[menu_bar][lock]") {
    bar_harness h;
    h.input.store_lock = model::domain::store_lock::absent;
    h.open_menu();
    h.click(bar_harness::row_centre(0));
    CHECK(h.result.unlock_requested);
    CHECK_FALSE(h.result.focus_instances);
}

namespace {

/** @brief Two fake top-level labels on a bar of their own, to drive the switching. */
struct two_labels final {
    headless_imgui imgui;
    menu_bar_state state;
    ImRect first;
    ImRect second;
    int rows_a = 0;
    int rows_b = 0;

    two_labels() {
        theme::apply_theme();
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 40.0f));
        ImGui::Begin("bar", nullptr, ImGuiWindowFlags_NoDecoration);
        widget::menu_bar_label(state, 0, "File", [&] {
            ++rows_a;
            static_cast<void>(widget::menu_item_row({.label = "Open"}));
        });
        first = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SameLine(0.0f, 0.0f);
        widget::menu_bar_label(state, 1, "View", [&] {
            ++rows_b;
            static_cast<void>(widget::menu_item_row({.label = "Zoom"}));
        });
        second = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::End();
        ImGui::Render();
    }

    void settle() {
        for (int i = 0; i < 3; ++i)
            frame();
    }

    void hover(const ImRect& box) {
        ImGui::GetIO().AddMousePosEvent((box.Min.x + box.Max.x) * 0.5f,
            (box.Min.y + box.Max.y) * 0.5f);
        settle();
    }

    void click(const ImRect& box) {
        hover(box);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        settle();
    }
};

}

TEST_CASE("with one menu open, hovering another label switches to it",
    "[menu_bar]") {
    two_labels bar;
    bar.settle();
    bar.click(bar.first);
    CHECK(bar.state.open == 0);
    CHECK(popup_open_in("bar", "File"));
    bar.hover(bar.second);
    CHECK(bar.state.open == 1);
    CHECK(popup_open_in("bar", "View"));
    CHECK_FALSE(popup_open_in("bar", "File"));
    bar.hover(bar.first);
    CHECK(bar.state.open == 0);
    CHECK(popup_open_in("bar", "File"));
    CHECK_FALSE(popup_open_in("bar", "View"));
}

TEST_CASE("hovering a label opens nothing while no menu is open", "[menu_bar]") {
    two_labels bar;
    bar.settle();
    bar.hover(bar.first);
    bar.hover(bar.second);
    CHECK(bar.state.open == menu_bar_state::none);
    CHECK(bar.rows_a == 0);
    CHECK(bar.rows_b == 0);
}

TEST_CASE("a menu's rows are drawn only while it is open", "[menu_bar]") {
    two_labels bar;
    bar.settle();
    CHECK(bar.rows_a == 0);
    bar.click(bar.second);
    CHECK(bar.rows_b > 0);
    CHECK(bar.rows_a == 0);
}

TEST_CASE("the work area is inset by the bar exactly once, and the desk starts "
          "at its bottom",
    "[menu_bar][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    render::workspace workspace;
    auto input = desk.input();
    for (int i = 0; i < 4; ++i) {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindow* bar = ImGui::FindWindowByName(bar_window);
    const ImGuiWindow* space = ImGui::FindWindowByName("###hedgr-desk");
    REQUIRE(bar != nullptr);
    REQUIRE(space != nullptr);
    const float bar_bottom = bar->Pos.y + bar->Size.y;
    CHECK(bar_bottom == Catch::Approx(theme::menu_bar_height));
    CHECK(viewport->WorkPos.y == Catch::Approx(bar_bottom));
    CHECK(space->Pos.y == Catch::Approx(bar_bottom));
    CHECK(viewport->WorkSize.y
        == Catch::Approx(viewport->Size.y - theme::menu_bar_height));
    /* The cool bar's own inset at the foot is still taken from the same area. */
    CHECK(space->Pos.y + space->Size.y
        <= viewport->WorkPos.y + viewport->WorkSize.y + 0.5f);
}

TEST_CASE("the workspace draws the bar before everything and the bar is not "
          "shown to a locked store's windows",
    "[menu_bar][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    render::workspace workspace;
    auto input = desk.input();
    input.store_lock = model::domain::store_lock::locked;
    for (int i = 0; i < 4; ++i) {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    }
    const ImGuiWindow* bar = ImGui::FindWindowByName(bar_window);
    REQUIRE(bar != nullptr);
    CHECK(bar->Active);
    CHECK(bar->Size.y == Catch::Approx(theme::menu_bar_height));
}

TEST_CASE("through the workspace, the system menu's Lock row locks the store",
    "[menu_bar][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    render::workspace workspace;
    auto input = desk.input();
    const auto frame = [&] {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    };
    for (int i = 0; i < 4; ++i)
        frame();
    const auto click = [&](ImVec2 at) {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        frame();
    };
    click(bar_harness::logo_centre());
    click(bar_harness::row_centre(1));
    bool locked = false;
    bool instances = false;
    for (auto& c : desk.commands.drain()) {
        locked = locked || c.kind == model::command::command_kind::lock_store;
        instances = instances
            || c.kind == model::command::command_kind::open_window;
    }
    CHECK(locked);
    CHECK_FALSE(instances);

    click(bar_harness::logo_centre());
    click(bar_harness::row_centre(0));
    for (auto& c : desk.commands.drain())
        instances = instances
            || (c.kind == model::command::command_kind::open_window
                && c.new_window_kind
                    == model::domain::window_kind::instance_manager);
    CHECK(instances);
}
