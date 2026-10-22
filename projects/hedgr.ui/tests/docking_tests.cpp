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
#include "imgui.h"
#include "imgui_internal.h"
#include <catch2/catch_test_macros.hpp>
#include <format>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
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

TEST_CASE("a chart alone in its dock node hides the tab bar and others keep it",
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
    const ImGuiWindow* browser = desk_window("###right");
    REQUIRE(chart != nullptr);
    REQUIRE(browser != nullptr);
    REQUIRE(chart->DockNode != nullptr);
    REQUIRE(browser->DockNode != nullptr);
    CHECK(chart->DockNode->IsHiddenTabBar());
    CHECK_FALSE(browser->DockNode->IsHiddenTabBar());
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
    CHECK(a->DockNode->IsHiddenTabBar());
    CHECK(b->DockNode->IsHiddenTabBar());
}

TEST_CASE("a docked chart's Pop out icon floats it at the size it floated at",
    "[docking][workspace]") {
    dock_desk d{"c"};
    const ImVec2 floating = desk_window("###c")->Size;
    REQUIRE(d.dock("###c"));
    REQUIRE(d.in_desk("###c"));
    REQUIRE(desk_window("###c")->Size.x != floating.x);

    REQUIRE(click_header_icon(d.workspace, d.input, "###c", "pop-out",
        hedgr::ui::icon::move));
    desk_frame(d.workspace, d.input);

    const ImGuiWindow* w = desk_window("###c");
    REQUIRE(w != nullptr);
    CHECK_FALSE(w->DockIsActive);
    CHECK(w->DockId == 0);
    CHECK(w->Size.x == floating.x);
    CHECK(w->Size.y == floating.y);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    CHECK(w->Pos.x >= viewport->WorkPos.x);
    CHECK(w->Pos.y + w->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y);
}

TEST_CASE("a docked chart's close icon closes the window",
    "[docking][workspace]") {
    dock_desk d{"c"};
    REQUIRE(d.dock("###c"));
    static_cast<void>(d.desk.commands.drain());

    REQUIRE(click_header_icon(d.workspace, d.input, "###c", "close",
        hedgr::ui::icon::cancel));

    std::vector<std::string> closed;
    for (const auto& c : d.desk.commands.drain()) {
        if (c.kind == model::command::command_kind::close_window)
            closed.push_back(c.window_id);
    }
    REQUIRE(closed.size() == 1);
    CHECK(closed[0] == "c");
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

TEST_CASE("a docked chart stops above the shortcut bar",
    "[docking][workspace]") {
    dock_desk d{"c"};
    model::domain::shortcut row;
    row.id = "s";
    row.target_kind = model::domain::window_kind::instance_manager;
    row.target_id = "i";
    d.desk.shortcuts.upsert(row);
    d.settle();
    REQUIRE(d.dock("###c"));

    const ImGuiWindow* bar = desk_window("##hedgr-shortcuts");
    REQUIRE(bar != nullptr);
    const ImGuiWindow* chart = desk_window("###c");
    REQUIRE(d.in_desk("###c"));
    CHECK(chart->Pos.y + chart->Size.y <= bar->Pos.y + 0.5f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    CHECK(chart->Pos.y + chart->Size.y
        < viewport->WorkPos.y + viewport->WorkSize.y);
}

TEST_CASE("the startup notice and the shortcut bar stay out of the desk",
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
    SECTION("the shortcut bar") {
        dock_desk d{};
        model::domain::shortcut row;
        row.id = "s";
        row.target_kind = model::domain::window_kind::instance_manager;
        row.target_id = "i";
        d.desk.shortcuts.upsert(row);
        d.settle();
        ImGui::DockBuilderDockWindow("##hedgr-shortcuts",
            d.workspace.dock_space());
        d.settle();
        const ImGuiWindow* bar = desk_window("##hedgr-shortcuts");
        REQUIRE(bar != nullptr);
        CHECK_FALSE(bar->DockIsActive);
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
