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
#include <string>
#include <utility>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;
using model::command::command;
using model::command::command_kind;
using model::domain::window_kind;

/** @brief A desk whose windows float side by side at known places. */
struct open_desk final {
    headless_imgui imgui;
    desk desk;
    render::workspace workspace;
    render::render_input input;
    std::string settings;
    float next_x = 20.0f;

    open_desk() : input(desk.input()) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    void place(const std::string& id, float width, float height) {
        settings += std::format("[Window][###{}]\nPos={},40\nSize={},{}\n\n",
            id, next_x, width, height);
        next_x += width + 40.0f;
    }

    void start() {
        ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());
        settle();
    }

    void frame() {
        ImGui::NewFrame();
        workspace.render(input);
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

    void press() {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
    }

    void release() {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    }

    void click(ImVec2 at) {
        hover(at);
        press();
        release();
    }

    /** @brief Presses at @p from, moves in steps to @p to and releases. */
    void drag(ImVec2 from, ImVec2 to) {
        hover(from);
        press();
        for (int step = 1; step <= 8; ++step) {
            const float t = static_cast<float>(step) / 8.0f;
            hover(ImVec2(from.x + (to.x - from.x) * t,
                from.y + (to.y - from.y) * t));
        }
        release();
    }

    [[nodiscard]] const ImGuiWindow* window(const char* stable_id) const {
        const ImGuiWindow* w = ImGui::FindWindowByName(stable_id);
        return w != nullptr && w->Active ? w : nullptr;
    }

    /** @brief Clicks the header icon @p id showing @p glyph of chart @p name. */
    bool click_chart_icon(const char* name, const char* id, const char* glyph) {
        const ImGuiWindow* w = window(name);
        if (w == nullptr)
            return false;
        const ImGuiID button = ImHashStr(glyph, 0,
            ImHashStr(id, 0, ImHashStr("chart", 0, w->ID)));
        const float y = w->DC.CursorStartPos.y + 6.0f;
        for (float x = w->Pos.x + w->Size.x - 1.0f; x > w->Pos.x; x -= 2.0f) {
            hover(ImVec2(x, y));
            if (ImGui::GetCurrentContext()->HoveredId == button) {
                click(ImVec2(x, y));
                return true;
            }
        }
        return false;
    }

    /** @brief The open requests since the last call, without view saves. */
    [[nodiscard]] std::vector<command> take() {
        std::vector<command> opens;
        for (auto& c : desk.commands.drain()) {
            if (c.kind == command_kind::open_window)
                opens.push_back(std::move(c));
        }
        return opens;
    }
};

void require_mini_plot_for(const command& c, const char* series,
    const char* label) {
    CHECK(c.kind == command_kind::open_window);
    CHECK(c.new_window_kind == window_kind::mini_plot);
    CHECK(c.instance_id == "i");
    CHECK(c.account_id == "a");
    CHECK(c.series_id == series);
    CHECK(c.series_label == label);
}

void add_chart(open_desk& d, const char* id, const char* series) {
    static_cast<void>(d.desk.open(window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = series},
        id));
    d.place(id, 560.0f, 400.0f);
}

void add_card(open_desk& d, const char* id, const char* series) {
    static_cast<void>(d.desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD",
            .series_id = series},
        id));
    d.place(id, 240.0f, 180.0f);
}

}

TEST_CASE("a floating chart's Mini plot icon opens a card for its series",
    "[mini_plot][open]") {
    open_desk d;
    add_chart(d, "c", "s-7");
    d.start();

    REQUIRE(d.click_chart_icon("###c", "mini", hedgr::ui::icon::dashboard));
    const auto asked = d.take();
    REQUIRE(asked.size() == 1);
    require_mini_plot_for(asked.front(), "s-7", "FX GBP/USD");
}

TEST_CASE("a docked chart's Mini plot icon opens a card for its series",
    "[mini_plot][open]") {
    open_desk d;
    add_chart(d, "c", "s-7");
    d.start();
    REQUIRE(d.click_chart_icon("###c", "dock", hedgr::ui::icon::panel));
    d.settle();
    REQUIRE(d.window("###c")->DockIsActive);
    static_cast<void>(d.take());

    REQUIRE(d.click_chart_icon("###c", "mini", hedgr::ui::icon::dashboard));
    const auto asked = d.take();
    REQUIRE(asked.size() == 1);
    require_mini_plot_for(asked.front(), "s-7", "FX GBP/USD");
}

TEST_CASE("asking twice for a series' card asks for the same window",
    "[mini_plot][open]") {
    open_desk d;
    add_chart(d, "c", "s-7");
    d.start();

    REQUIRE(d.click_chart_icon("###c", "mini", hedgr::ui::icon::dashboard));
    REQUIRE(d.click_chart_icon("###c", "mini", hedgr::ui::icon::dashboard));
    const auto asked = d.take();
    REQUIRE(asked.size() == 2);
    require_mini_plot_for(asked[0], "s-7", "FX GBP/USD");
    require_mini_plot_for(asked[1], "s-7", "FX GBP/USD");
}

TEST_CASE("a series browser row's Mini plot icon opens a card for that row",
    "[mini_plot][open]") {
    open_desk d;
    d.desk.market.apply_catalogue("i", "a",
        {model::domain::market_series_row{.id = "s-0", .label = "FX GBP/USD",
             .asset_class = "FX", .provider_code = "ore"},
            model::domain::market_series_row{.id = "s-1",
                .label = "FX EUR/USD", .asset_class = "FX",
                .provider_code = "ore"}});
    static_cast<void>(d.desk.open(window_kind::series_browser,
        model::domain::series_browser_state{}, "b"));
    d.place("b", 900.0f, 500.0f);
    d.start();

    const ImGuiWindow* w = d.window("###b");
    REQUIRE(w != nullptr);
    std::vector<command> asked;
    /*
     * The icon is found by clicking from the right edge inwards along each
     * line of the table, so the test depends on the icon being in the row and
     * not on where. A press on the row itself opens nothing.
     */
    for (float y = w->Pos.y + 40.0f; y < w->Pos.y + 320.0f && asked.size() < 2;
        y += 4.0f) {
        for (float x = w->Pos.x + w->Size.x - 6.0f;
            x > w->Pos.x + w->Size.x - 60.0f; x -= 4.0f) {
            d.click(ImVec2(x, y));
            auto got = d.take();
            if (!got.empty()) {
                for (auto& c : got)
                    asked.push_back(std::move(c));
                y += ImGui::GetTextLineHeight()
                    + 2.0f * ImGui::GetStyle().CellPadding.y - 4.0f;
                break;
            }
        }
    }
    /* The browser lists by label, so the EUR row comes first. */
    REQUIRE(asked.size() == 2);
    require_mini_plot_for(asked[0], "s-1", "FX EUR/USD");
    require_mini_plot_for(asked[1], "s-0", "FX GBP/USD");
}

TEST_CASE("clicking a card opens the chart for its series",
    "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);

    d.click(ImVec2(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.6f));
    const auto asked = d.take();
    REQUIRE(asked.size() == 1);
    CHECK(asked.front().kind == command_kind::open_window);
    CHECK(asked.front().new_window_kind == window_kind::chart);
    CHECK(asked.front().instance_id == "i");
    CHECK(asked.front().account_id == "a");
    CHECK(asked.front().series_id == "s-3");
    CHECK(asked.front().series_label == "FX GBP/USD");
}

TEST_CASE("dragging on a card's tile opens no chart", "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 at(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.6f);

    d.drag(at, ImVec2(at.x + 60.0f, at.y + 40.0f));
    CHECK(d.take().empty());
}

TEST_CASE("a floating card is still moved by its title", "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 before = w->Pos;
    const ImVec2 title(w->Pos.x + w->Size.x * 0.5f,
        w->Pos.y + ImGui::GetFrameHeight() * 0.5f);

    d.drag(title, ImVec2(title.x + 80.0f, title.y + 60.0f));
    d.settle();
    const ImGuiWindow* after = d.window("###m");
    REQUIRE(after != nullptr);
    CHECK(after->Pos.x > before.x + 40.0f);
    CHECK(after->Pos.y > before.y + 30.0f);
    CHECK(d.take().empty());
}

TEST_CASE("a docked card is still undocked by dragging its tab",
    "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    add_card(d, "n", "s-4");
    d.start();
    ImGui::NewFrame();
    {
        const ImGuiID root = ImGui::GetID("test-dock");
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodePos(root, ImVec2(20.0f, 20.0f));
        ImGui::DockBuilderSetNodeSize(root, ImVec2(900.0f, 500.0f));
        ImGuiID right = 0;
        const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
            0.5f, nullptr, &right);
        ImGui::DockBuilderDockWindow("###m", left);
        ImGui::DockBuilderDockWindow("###n", right);
        ImGui::DockBuilderFinish(root);
    }
    d.workspace.render(d.input);
    ImGui::Render();
    d.settle();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    REQUIRE(w->DockIsActive);
    const ImGuiTabItem* tab =
        ImGui::TabBarFindTabByID(w->DockNode->TabBar, w->TabId);
    REQUIRE(tab != nullptr);
    const ImGuiTabBar* bar = w->DockNode->TabBar;
    const ImVec2 grab(bar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f,
        (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f);

    d.hover(grab);
    d.press();
    for (float step = 1.0f; step <= 8.0f; step += 1.0f)
        d.hover(ImVec2(grab.x + 20.0f * step, grab.y + 40.0f * step));
    d.release();
    d.settle();

    const ImGuiWindow* after = d.window("###m");
    REQUIRE(after != nullptr);
    CHECK_FALSE(after->DockIsActive);
    CHECK(d.take().empty());
}
