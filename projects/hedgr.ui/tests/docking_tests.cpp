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
#include "imgui.h"
#include "imgui_internal.h"
#include <catch2/catch_test_macros.hpp>

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
