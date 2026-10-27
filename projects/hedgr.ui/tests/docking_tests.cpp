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
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <format>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;

/* ImGui hashes only what follows ###, so the stable id finds the window. */
const ImGuiWindow* desk_window(const char* stable_id) {
    const ImGuiWindow* w = ImGui::FindWindowByName(stable_id);
    return w != nullptr && w->Active ? w : nullptr;
}

/*
 * A floating dock node split in two, with one window named into each half.
 * The dock builder is the code path a drag ends in, so a window it docks is a
 * window an operator could have docked.
 */
void dock_side_by_side(const char* left_window, const char* right_window) {
    const ImGuiID root = ImGui::GetID("test-dock");
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
    ImGui::DockBuilderSetNodePos(root, ImVec2(20.0f, 20.0f));
    ImGui::DockBuilderSetNodeSize(root, ImVec2(1200.0f, 700.0f));
    ImGuiID right = 0;
    const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
        0.5f, nullptr, &right);
    ImGui::DockBuilderDockWindow(left_window, left);
    ImGui::DockBuilderDockWindow(right_window, right);
    ImGui::DockBuilderFinish(root);
}

}

TEST_CASE("two desk windows docked side by side are both drawn, docked",
    "[docking][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"},
        "left"));
    static_cast<void>(desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "right"));
    render::workspace workspace;
    auto input = desk.input();

    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        if (i == 0)
            dock_side_by_side("###left", "###right");
        workspace.render(input);
        ImGui::Render();
    }

    const ImGuiWindow* left = desk_window("###left");
    const ImGuiWindow* right = desk_window("###right");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->DockIsActive);
    CHECK(right->DockIsActive);
    CHECK(left->DockNode != right->DockNode);
    CHECK(left->Pos.x < right->Pos.x);
    CHECK_FALSE(left->DrawList->VtxBuffer.empty());
    CHECK_FALSE(right->DrawList->VtxBuffer.empty());
}

TEST_CASE("a chart alone in its dock node keeps its tab to drag it out by",
    "[docking][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"},
        "left"));
    static_cast<void>(desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "right"));
    render::workspace workspace;
    auto input = desk.input();

    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        if (i == 0)
            dock_side_by_side("###left", "###right");
        workspace.render(input);
        ImGui::Render();
    }

    const ImGuiWindow* chart = desk_window("###left");
    REQUIRE(chart != nullptr);
    REQUIRE(chart->DockNode != nullptr);
    CHECK_FALSE(chart->DockNode->IsHiddenTabBar());
    REQUIRE(chart->DockNode->TabBar != nullptr);
    CHECK(ImGui::TabBarFindTabByID(chart->DockNode->TabBar, chart->TabId)
        != nullptr);
}

TEST_CASE("the lock prompt stays out of a dock", "[docking][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "browser"));
    render::workspace workspace;
    auto input = desk.input();
    input.store_lock = model::domain::store_lock::locked;

    /* Docked once the prompt exists, since it reads no saved settings. */
    for (int i = 0; i < 4; ++i) {
        ImGui::NewFrame();
        if (i == 1)
            dock_side_by_side("###browser", "###hedgr-lock");
        workspace.render(input);
        ImGui::Render();
    }

    const ImGuiWindow* prompt = desk_window("###hedgr-lock");
    REQUIRE(prompt != nullptr);
    CHECK_FALSE(prompt->DockIsActive);
}

namespace {

/** @brief One frame of the whole desk. */
void desk_frame(render::workspace& workspace, const render::render_input& input) {
    ImGui::NewFrame();
    workspace.render(input);
    ImGui::Render();
}

/** @brief Moves the mouse to @p at and draws a frame. */
void hover(render::workspace& workspace, const render::render_input& input,
    ImVec2 at) {
    ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    desk_frame(workspace, input);
}

/** @brief Presses and releases the left button at @p at, a frame for each. */
void click(render::workspace& workspace, const render::render_input& input,
    ImVec2 at) {
    hover(workspace, input, at);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    desk_frame(workspace, input);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    desk_frame(workspace, input);
}

/**
 * @brief Clicks the icon @p id, showing @p glyph, in a chart's header row.
 *
 * Found by hovering along the row until ImGui reports the button hovered, so
 * the test depends on the icon being in the header and not on where.
 */
bool click_header_icon(render::workspace& workspace,
    const render::render_input& input, const char* window, const char* id,
    const char* glyph) {
    const ImGuiWindow* w = desk_window(window);
    if (w == nullptr)
        return false;
    const ImGuiID button = ImHashStr(glyph, 0,
        ImHashStr(id, 0, ImHashStr("chart", 0, w->ID)));
    const float y = w->DC.CursorStartPos.y + 6.0f;
    const float left = w->Pos.x;
    for (float x = w->Pos.x + w->Size.x - 1.0f; x > left; x -= 2.0f) {
        hover(workspace, input, ImVec2(x, y));
        if (ImGui::GetCurrentContext()->HoveredId == button) {
            click(workspace, input, ImVec2(x, y));
            return true;
        }
    }
    return false;
}

/** @brief A desk with docking on and the charts named in @p ids, floating. */
struct dock_desk final {
    headless_imgui imgui;
    desk desk;
    render::workspace workspace;
    render::render_input input;

    explicit dock_desk(std::initializer_list<const char*> ids)
        : input(desk.input()) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        /* Side by side, so no chart's header is under another chart. */
        std::string settings;
        float x = 20.0f;
        for (const char* id : ids) {
            static_cast<void>(desk.open(model::domain::window_kind::chart,
                model::domain::chart_state{.label = "FX GBP/USD",
                    .series_id = id},
                id));
            settings += std::format(
                "[Window][###{}]\nPos={},40\nSize=560,400\n\n", id, x);
            x += 600.0f;
        }
        ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());
        settle();
    }

    void settle() {
        for (int i = 0; i < 3; ++i)
            desk_frame(workspace, input);
    }

    /** @brief Whether @p window is docked somewhere in the desk's dock space. */
    [[nodiscard]] bool in_desk(const char* window) const {
        const ImGuiWindow* w = desk_window(window);
        return w != nullptr && w->DockIsActive && w->DockNode != nullptr
            && ImGui::DockNodeGetRootNode(w->DockNode)->ID
                == workspace.dock_space();
    }

    bool dock(const char* window) {
        const bool clicked = click_header_icon(workspace, input, window, "dock",
            hedgr::ui::icon::panel);
        settle();
        return clicked;
    }
};

}

TEST_CASE("a floating chart's Dock icon docks it into the desk",
    "[docking][workspace]") {
    dock_desk d{"c"};
    REQUIRE(d.workspace.dock_space() != 0);
    REQUIRE_FALSE(d.in_desk("###c"));

    REQUIRE(d.dock("###c"));
    CHECK(d.in_desk("###c"));
    const ImGuiDockNode* centre =
        ImGui::DockBuilderGetCentralNode(d.workspace.dock_space());
    REQUIRE(centre != nullptr);
    CHECK(desk_window("###c")->DockNode == centre);
}

TEST_CASE("a second chart docked by its icon splits beside the first",
    "[docking][workspace]") {
    dock_desk d{"a", "b"};
    REQUIRE(d.dock("###a"));
    REQUIRE(d.dock("###b"));

    REQUIRE(d.in_desk("###a"));
    REQUIRE(d.in_desk("###b"));
    const ImGuiWindow* a = desk_window("###a");
    const ImGuiWindow* b = desk_window("###b");
    /* Side by side and both shown, not two tabs of one node. */
    CHECK(a->DockNode != b->DockNode);
    CHECK(a->Pos.x < b->Pos.x);
    CHECK_FALSE(a->DockNode->IsHiddenTabBar());
    CHECK_FALSE(b->DockNode->IsHiddenTabBar());
}

TEST_CASE("dragging a docked chart's tab out floats it",
    "[docking][workspace]") {
    dock_desk d{"c"};
    REQUIRE(d.dock("###c"));
    REQUIRE(d.in_desk("###c"));

    const ImGuiWindow* w = desk_window("###c");
    REQUIRE(w->DockNode->TabBar != nullptr);
    const ImGuiTabItem* tab =
        ImGui::TabBarFindTabByID(w->DockNode->TabBar, w->TabId);
    REQUIRE(tab != nullptr);
    const ImGuiTabBar* bar = w->DockNode->TabBar;
    const ImVec2 grab(bar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f,
        (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f);

    hover(d.workspace, d.input, grab);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    desk_frame(d.workspace, d.input);
    for (float step = 1.0f; step <= 8.0f; step += 1.0f) {
        hover(d.workspace, d.input, ImVec2(grab.x + 20.0f * step,
            grab.y + 40.0f * step));
    }
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    d.settle();

    const ImGuiWindow* after = desk_window("###c");
    REQUIRE(after != nullptr);
    CHECK_FALSE(after->DockIsActive);
    CHECK(after->DockId == 0);
}

TEST_CASE("a docked chart has no Dock icon to click",
    "[docking][workspace]") {
    dock_desk d{"c"};
    REQUIRE(d.dock("###c"));
    CHECK_FALSE(click_header_icon(d.workspace, d.input, "###c", "dock",
        hedgr::ui::icon::panel));
}

TEST_CASE("the desk's empty centre lets a click through to the background",
    "[docking][workspace]") {
    dock_desk d{};
    REQUIRE(d.workspace.dock_space() != 0);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 centre(viewport->Pos.x + viewport->Size.x * 0.5f,
        viewport->Pos.y + viewport->Size.y * 0.5f);

    hover(d.workspace, d.input, centre);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    desk_frame(d.workspace, d.input);
    CHECK(ImGui::GetCurrentContext()->HoveredWindow == nullptr);
    CHECK_FALSE(ImGui::GetIO().WantCaptureMouse);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    desk_frame(d.workspace, d.input);
}

TEST_CASE("the dock space reaches the bottom of the work area, shortcuts or not",
    "[docking][workspace]") {
    dock_desk d{"c"};
    model::domain::shortcut row;
    row.id = "s";
    row.target_kind = model::domain::window_kind::accounts;
    row.target_id = "i";
    d.desk.shortcuts.upsert(row);
    d.settle();
    REQUIRE(d.dock("###c"));

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindow* space = desk_window("###hedgr-desk");
    REQUIRE(space != nullptr);
    CHECK(space->Pos.y + space->Size.y
        == Catch::Approx(viewport->WorkPos.y + viewport->WorkSize.y));
    REQUIRE(d.in_desk("###c"));
    const ImGuiWindow* chart = desk_window("###c");
    CHECK(chart->Pos.y + chart->Size.y
        <= viewport->WorkPos.y + viewport->WorkSize.y + 0.5f);
    CHECK(desk_window("##hedgr-shortcuts") == nullptr);
}

TEST_CASE("the startup notice and the lock prompt stay out of the desk",
    "[docking][workspace]") {
    SECTION("the startup notice") {
        dock_desk d{};
        d.input.startup_failure = "The store could not be opened";
        d.input.store_lock = model::domain::store_lock::absent;
        d.settle();
        ImGui::DockBuilderDockWindow("###hedgr-startup-failure",
            d.workspace.dock_space());
        d.settle();
        const ImGuiWindow* notice = desk_window("###hedgr-startup-failure");
        REQUIRE(notice != nullptr);
        CHECK_FALSE(notice->DockIsActive);
    }
    SECTION("the lock prompt") {
        dock_desk d{};
        d.input.store_lock = model::domain::store_lock::locked;
        d.settle();
        ImGui::DockBuilderDockWindow("###hedgr-lock", d.workspace.dock_space());
        d.settle();
        const ImGuiWindow* prompt = desk_window("###hedgr-lock");
        REQUIRE(prompt != nullptr);
        CHECK_FALSE(prompt->DockIsActive);
    }
}

namespace {

/** @brief Whether any window is docked under the desk's dock space. */
bool holds_docked_windows(ImGuiID space) {
    const auto walk = [](const auto& self, const ImGuiDockNode* node) -> bool {
        return node != nullptr
            && (!node->Windows.empty() || self(self, node->ChildNodes[0])
                || self(self, node->ChildNodes[1]));
    };
    return walk(walk, ImGui::DockBuilderGetNode(space));
}

/**
 * @brief Chooses VIEW > Undock all, finding VIEW by sweeping along the top bar
 * until ImGui reports its label hovered, and the row as the popup's first.
 */
bool click_undock_all(dock_desk& d) {
    const ImGuiWindow* bar = desk_window("###hedgr-menu-bar");
    if (bar == nullptr)
        return false;
    const ImGuiID label = ImHashStr("##label", 0, ImHashStr("View", 0, bar->ID));
    const float y = bar->Pos.y + bar->Size.y * 0.5f;
    for (float x = bar->Pos.x + 2.0f; x < bar->Pos.x + 600.0f; x += 2.0f) {
        hover(d.workspace, d.input, ImVec2(x, y));
        if (ImGui::GetCurrentContext()->HoveredId != label)
            continue;
        click(d.workspace, d.input, ImVec2(x, y));
        d.settle();
        const ImGuiContext& context = *ImGui::GetCurrentContext();
        if (context.OpenPopupStack.Size == 0)
            return false;
        const ImGuiWindow* popup = context.OpenPopupStack.back().Window;
        if (popup == nullptr)
            return false;
        click(d.workspace, d.input,
            ImVec2(popup->Pos.x + theme::menu_padding + 30.0f,
                popup->Pos.y + theme::menu_padding
                    + (ImGui::GetTextLineHeight()
                        + 2.0f * theme::menu_row_padding_y) * 0.5f));
        return true;
    }
    return false;
}

/**
 * @brief Docks five windows into the desk as a grid with a tabbed node.
 *
 * One window on the left, one top right, and three as tabs under it, so the
 * tree holds a split inside a split and a node with several tabs. The one on
 * the left is the last opened, so the walk reaches the nested ones first, while
 * the tree is still whole.
 */
void dock_grid_with_tabs(dock_desk& d) {
    const ImGuiID space = d.workspace.dock_space();
    const ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(space);
    REQUIRE(centre != nullptr);
    ImGuiID right = 0;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre->ID, ImGuiDir_Left,
        0.5f, nullptr, &right);
    ImGuiID bottom = 0;
    const ImGuiID top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f,
        nullptr, &bottom);
    ImGui::DockBuilderDockWindow("###e", left);
    ImGui::DockBuilderDockWindow("###a", top);
    ImGui::DockBuilderDockWindow("###b", bottom);
    ImGui::DockBuilderDockWindow("###c", bottom);
    ImGui::DockBuilderDockWindow("###d", bottom);
    d.settle();
}

}

TEST_CASE("the undock control floats every docked window and only those",
    "[docking][workspace]") {
    dock_desk d{"a", "b", "c", "d", "e", "f"};
    static_cast<void>(d.desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "g"));
    d.settle();
    REQUIRE(d.workspace.dock_space() != 0);
    dock_grid_with_tabs(d);
    const char* docked[] = {"###a", "###b", "###c", "###d", "###e"};
    for (const char* id : docked)
        REQUIRE(d.in_desk(id));
    const ImGuiWindow* floating = desk_window("###f");
    REQUIRE(floating != nullptr);
    const ImVec2 floating_pos = floating->Pos;
    const ImVec2 floating_size = floating->Size;
    REQUIRE(desk_window("###a")->DockNode
        != desk_window("###c")->DockNode);
    REQUIRE(desk_window("###b")->DockNode == desk_window("###d")->DockNode);
    REQUIRE(desk_window("###b")->DockNode->ParentNode->ParentNode != nullptr);

    REQUIRE(click_undock_all(d));
    d.settle();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    std::vector<ImVec2> origins;
    for (const char* id : docked) {
        const ImGuiWindow* w = desk_window(id);
        REQUIRE(w != nullptr);
        INFO(id);
        CHECK_FALSE(w->DockIsActive);
        CHECK(w->DockId == 0);
        CHECK(w->Pos.x >= viewport->WorkPos.x);
        CHECK(w->Pos.y >= viewport->WorkPos.y);
        CHECK(w->Pos.x + w->Size.x
            <= viewport->WorkPos.x + viewport->WorkSize.x);
        CHECK(w->Pos.y + w->Size.y
            <= viewport->WorkPos.y + viewport->WorkSize.y);
        CHECK(w->Size.x == 560.0f);
        CHECK(w->Size.y == 400.0f);
        for (const ImVec2& other : origins)
            CHECK((other.x != w->Pos.x || other.y != w->Pos.y));
        origins.push_back(w->Pos);
    }
    CHECK(desk_window("###f")->Pos.x == floating_pos.x);
    CHECK(desk_window("###f")->Pos.y == floating_pos.y);
    CHECK(desk_window("###f")->Size.x == floating_size.x);
    CHECK(desk_window("###f")->Size.y == floating_size.y);
    CHECK_FALSE(desk_window("###g")->DockIsActive);
    CHECK_FALSE(holds_docked_windows(d.workspace.dock_space()));
}

TEST_CASE("VIEW > Undock all floats a docked window",
    "[docking][workspace]") {
    dock_desk d{"a"};
    REQUIRE(d.dock("###a"));
    REQUIRE(d.in_desk("###a"));
    REQUIRE(click_undock_all(d));
    d.settle();
    CHECK_FALSE(d.in_desk("###a"));
}

namespace {

namespace theme = hedgr::ui::theme;

/**
 * @brief Docks "a" and "b" into the desk's space, side by side, as the dock
 * builder does for a drag, under the theme the desk runs with.
 */
void dock_pair(dock_desk& d) {
    theme::apply_theme();
    const ImGuiID space = d.workspace.dock_space();
    ImGuiID right = 0;
    const ImGuiID left = ImGui::DockBuilderSplitNode(space, ImGuiDir_Left, 0.5f,
        nullptr, &right);
    ImGui::DockBuilderDockWindow("###a", left);
    ImGui::DockBuilderDockWindow("###b", right);
    ImGui::DockBuilderFinish(space);
    d.settle();
    d.settle();
}

/** @brief A themed desk with charts "a" and "b" docked side by side. */
struct docked_pair final {
    dock_desk d{"a", "b"};

    docked_pair() {
        dock_pair(d);
    }

    [[nodiscard]] const ImGuiDockNode& node(const char* window) const {
        return *desk_window(window)->DockNode;
    }

    /** @brief The distance from @p at to the nearest vertex the node's host drew. */
    [[nodiscard]] float nearest(const char* window, ImVec2 at) const {
        float best = 1.0e9f;
        for (const ImDrawVert& v : node(window).HostWindow->DrawList->VtxBuffer)
            best = std::min(best, std::hypot(v.pos.x - at.x, v.pos.y - at.y));
        return best;
    }
};

}

TEST_CASE("the tabs of the desk's docked windows are faded by the desk frame",
    "[docking][workspace][tabs]") {
    dock_desk d{"a", "b"};
    dock_pair(d);

    const ImGuiDockNode* node = desk_window("###a")->DockNode;
    REQUIRE(node != nullptr);
    REQUIRE(node->HostWindow != nullptr);
    const auto& buffer = node->HostWindow->DrawList->VtxBuffer;
    const auto has = [&](ImU32 col) {
        return std::ranges::any_of(buffer,
            [&](const ImDrawVert& v) { return v.col == col; });
    };
    /* ImGui paints a tab flat, so a head colour is the fade's doing. */
    CHECK(has(theme::tab_active_top));
    CHECK(has(ImGui::ColorConvertFloat4ToU32(theme::tab_active_colour())));
}

TEST_CASE("two docked windows are a gap apart", "[docking][workspace][look]") {
    docked_pair p;
    const ImGuiWindow* a = desk_window("###a");
    const ImGuiWindow* b = desk_window("###b");
    REQUIRE(p.d.in_desk("###a"));
    REQUIRE(p.d.in_desk("###b"));
    REQUIRE(a->Pos.x < b->Pos.x);
    CHECK(ImGui::GetStyle().DockingSeparatorSize == theme::dock_gap);
    CHECK(b->Pos.x - (a->Pos.x + a->Size.x) == Catch::Approx(theme::dock_gap));
}

TEST_CASE("a docked window's body is a card, rounded at the bottom and square "
          "under its strip",
    "[docking][workspace][look]") {
    docked_pair p;
    const ImGuiDockNode& left = p.node("###a");
    const ImGuiDockNode& right = p.node("###b");
    const float top = left.TabBar->BarRect.Max.y;
    const float foot = left.Pos.y + left.Size.y;
    /* The corners beside the gap are the ones ImGui would have left square. */
    const float inner_left = left.Pos.x + left.Size.x;
    const float inner_right = right.Pos.x;
    CHECK(p.nearest("###a", ImVec2(inner_left, foot)) > 1.5f);
    CHECK(p.nearest("###b", ImVec2(inner_right, foot)) > 1.5f);
    CHECK(p.nearest("###a", ImVec2(left.Pos.x, foot)) > 1.5f);
    /* A vertex on the cut-back corner is dock_rounding away from it. */
    CHECK(p.nearest("###a", ImVec2(inner_left - theme::dock_rounding * 0.3f,
              foot - theme::dock_rounding * 0.3f))
        < theme::dock_rounding);
    /* Under the strip the body meets it square, on a vertex. */
    CHECK(p.nearest("###a", ImVec2(inner_left, top)) < 1.0f);
}

TEST_CASE("a docked window's strip is rounded at its top corners, the gap "
          "side included",
    "[docking][workspace][look]") {
    docked_pair p;
    const ImGuiDockNode& left = p.node("###a");
    const ImGuiDockNode& right = p.node("###b");
    CHECK(p.nearest("###a", ImVec2(left.Pos.x + left.Size.x, left.Pos.y)) > 1.5f);
    CHECK(p.nearest("###b", ImVec2(right.Pos.x, right.Pos.y)) > 1.5f);
}

TEST_CASE("a docked window draws no fill of its own", "[docking][workspace][look]") {
    docked_pair p;
    /* ImGui's square fill would put a vertex on the gap's corner. */
    const ImGuiWindow* a = desk_window("###a");
    const ImGuiDockNode& left = p.node("###a");
    CHECK(p.nearest("###a", ImVec2(left.Pos.x + left.Size.x,
              left.Pos.y + left.Size.y)) > 1.5f);
    CHECK(a->DockIsActive);
}

namespace {

/** @brief The solid body vertices of @p node's card, below the tabs that end on its strip's foot. */
std::vector<ImDrawVert> card_body(const ImGuiDockNode& node) {
    const float top = node.TabBar->BarRect.Max.y;
    std::vector<ImDrawVert> body;
    for (const ImDrawVert& v : node.HostWindow->DrawList->VtxBuffer)
        if (v.pos.y > top + 0.25f && v.pos.x >= node.Pos.x - 0.01f
            && v.pos.x <= node.Pos.x + node.Size.x + 0.01f
            && v.pos.y <= node.Pos.y + node.Size.y + 0.01f
            && (v.col >> IM_COL32_A_SHIFT) == 255u)
            body.push_back(v);
    return body;
}

/** @brief Whether @p c is within one eight-bit step per channel of @p want. */
bool near_colour(ImU32 c, ImU32 want, unsigned int slack) {
    for (int shift : {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT}) {
        const int a = static_cast<int>((c >> shift) & 0xffu);
        const int b = static_cast<int>((want >> shift) & 0xffu);
        if (std::abs(a - b) > static_cast<int>(slack))
            return false;
    }
    return true;
}

}

TEST_CASE("a docked card's body is the reference's content colour",
    "[docking][workspace][look]") {
    docked_pair p;
    const ImU32 content
        = ImGui::ColorConvertFloat4ToU32(theme::tab_bar_colour());
    for (const char* window : {"###a", "###b"}) {
        const auto body = card_body(p.node(window));
        REQUIRE_FALSE(body.empty());
        const auto [high, low] = std::ranges::minmax(body, {},
            [](const ImDrawVert& v) { return v.pos.y; });
        /* The fill's top and foot are the same flat colour. */
        CHECK(near_colour(high.col, content, 2));
        CHECK(near_colour(low.col, content, 2));
    }
}

TEST_CASE("a docked card keeps its rounded corners under the content fill",
    "[docking][workspace][look]") {
    docked_pair p;
    const ImGuiDockNode& left = p.node("###a");
    const float foot = left.Pos.y + left.Size.y;
    const float inner = left.Pos.x + left.Size.x;
    for (const ImDrawVert& v : card_body(left)) {
        /* A square fan would put a vertex on the corner itself. */
        CHECK(std::hypot(v.pos.x - inner, v.pos.y - foot) > 1.5f);
        CHECK(std::hypot(v.pos.x - left.Pos.x, v.pos.y - foot) > 1.5f);
    }
}

TEST_CASE("two cards stacked in the desk each have their own content fill",
    "[docking][workspace][look]") {
    dock_desk d{"a", "b"};
    theme::apply_theme();
    const ImU32 content
        = ImGui::ColorConvertFloat4ToU32(theme::tab_bar_colour());
    const ImGuiID space = d.workspace.dock_space();
    ImGuiID lower = 0;
    const ImGuiID upper = ImGui::DockBuilderSplitNode(space, ImGuiDir_Up, 0.5f,
        nullptr, &lower);
    ImGui::DockBuilderDockWindow("###a", upper);
    ImGui::DockBuilderDockWindow("###b", lower);
    ImGui::DockBuilderFinish(space);
    d.settle();
    d.settle();
    const ImGuiDockNode& a = *desk_window("###a")->DockNode;
    const ImGuiDockNode& b = *desk_window("###b")->DockNode;
    REQUIRE(a.Pos.y < b.Pos.y);
    for (const ImGuiDockNode* node : {&a, &b}) {
        const auto body = card_body(*node);
        REQUIRE_FALSE(body.empty());
        const auto [high, low] = std::ranges::minmax(body, {},
            [](const ImDrawVert& v) { return v.pos.y; });
        CHECK(near_colour(high.col, content, 2));
        CHECK(near_colour(low.col, content, 2));
    }
}

TEST_CASE("a floating window keeps its own rounded fill and no gap",
    "[docking][workspace][look]") {
    dock_desk d{"c"};
    theme::apply_theme();
    d.settle();
    const ImGuiWindow* w = desk_window("###c");
    REQUIRE(w != nullptr);
    REQUIRE_FALSE(w->DockIsActive);
    CHECK(w->WindowRounding == ImGui::GetStyle().WindowRounding);
    /* Its own fill, shaded by the window gradient to its foot colour. */
    CHECK(std::ranges::any_of(w->DrawList->VtxBuffer, [](const ImDrawVert& v) {
        return v.col == theme::window_gradient_bottom;
    }));
    /* And rounded: no vertex on its top-left. */
    float best = 1.0e9f;
    for (const ImDrawVert& v : w->DrawList->VtxBuffer)
        best = std::min(best, std::hypot(v.pos.x - w->Pos.x, v.pos.y - w->Pos.y));
    CHECK(best > 1.5f);
}
