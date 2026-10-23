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
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
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
    const char* label, std::int64_t span_ns) {
    CHECK(c.kind == command_kind::open_window);
    CHECK(c.new_window_kind == window_kind::mini_plot);
    CHECK(c.instance_id == "i");
    CHECK(c.account_id == "a");
    CHECK(c.series_id == series);
    CHECK(c.series_label == label);
    CHECK(c.series_span_ns == span_ns);
}

constexpr std::int64_t fifteen_minutes_ns = 15LL * 60 * 1'000'000'000;
constexpr std::int64_t one_hour_ns = 3600LL * 1'000'000'000;

/** @brief A chart whose own view plots 15 minutes. */
void add_chart(open_desk& d, const char* id, const char* series) {
    static_cast<void>(d.desk.open(window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = series,
            .view = model::domain::chart_view{.span_ns = fifteen_minutes_ns}},
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
    require_mini_plot_for(asked.front(), "s-7", "FX GBP/USD", fifteen_minutes_ns);
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
    require_mini_plot_for(asked.front(), "s-7", "FX GBP/USD", fifteen_minutes_ns);
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
    require_mini_plot_for(asked[0], "s-7", "FX GBP/USD", fifteen_minutes_ns);
    require_mini_plot_for(asked[1], "s-7", "FX GBP/USD", fifteen_minutes_ns);
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
    d.desk.market_view.chart_span = render::chart_span_index(one_hour_ns);
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
    require_mini_plot_for(asked[0], "s-1", "FX EUR/USD", one_hour_ns);
    require_mini_plot_for(asked[1], "s-0", "FX GBP/USD", one_hour_ns);
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
    const float left = w->Pos.x;

    d.drag(at, ImVec2(at.x + 60.0f, at.y + 40.0f));
    CHECK(d.take().empty());
    CHECK(d.window("###m")->Pos.x > left + 20.0f);
}

TEST_CASE("a card has no title bar", "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    add_chart(d, "c", "s-3");
    d.start();
    const ImGuiWindow* card = d.window("###m");
    const ImGuiWindow* chart = d.window("###c");
    REQUIRE(card != nullptr);
    REQUIRE(chart != nullptr);
    CHECK((card->Flags & ImGuiWindowFlags_NoTitleBar) != 0);
    CHECK((chart->Flags & ImGuiWindowFlags_NoTitleBar) == 0);
    CHECK(card->TitleBarHeight == 0.0f);
}

TEST_CASE("a floating card is moved by dragging its body", "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 before = w->Pos;
    const ImVec2 body(w->Pos.x + w->Size.x * 0.5f,
        w->Pos.y + w->Size.y * 0.5f);

    d.drag(body, ImVec2(body.x + 80.0f, body.y + 60.0f));
    d.settle();
    const ImGuiWindow* after = d.window("###m");
    REQUIRE(after != nullptr);
    CHECK(after->Pos.x > before.x + 40.0f);
    CHECK(after->Pos.y > before.y + 30.0f);
    CHECK_FALSE(after->DockIsActive);
    CHECK(d.take().empty());
}

TEST_CASE("a floating card is closed from its context menu",
    "[mini_plot][open]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 at(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);

    d.hover(at);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    d.frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    d.frame();
    d.frame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    REQUIRE(g.OpenPopupStack.Size == 1);
    const ImGuiWindow* menu = g.OpenPopupStack[0].Window;
    REQUIRE(menu != nullptr);
    d.click(ImVec2(menu->Pos.x + menu->WindowPadding.x + 12.0f,
        menu->Pos.y + menu->WindowPadding.y + ImGui::GetFontSize() * 0.5f));

    bool closed = false;
    for (auto& c : d.desk.commands.drain()) {
        if (c.kind == command_kind::close_window)
            closed = closed || c.window_id == "m";
    }
    CHECK(closed);
}

TEST_CASE("a docked card is still undocked by dragging its corner triangle",
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
    /* The node hides its tab bar, so the triangle is what is left to grab. */
    REQUIRE(w->DockNode->IsHiddenTabBar());
    const ImVec2 grab(w->DockNode->Pos.x + 3.0f, w->DockNode->Pos.y + 3.0f);

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

namespace {

/**
 * Five cards in a split tree: two stacked on the left, and on the right one
 * column beside a stack of two. Docked by the dock builder, which is the code
 * path a drag ends in.
 */
constexpr std::array<const char*, 5> grid_ids{"g1", "g2", "g3", "g4", "g5"};

void dock_grid() {
    const ImGuiID root = ImGui::GetID("test-dock");
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
    ImGui::DockBuilderSetNodePos(root, ImVec2(20.0f, 20.0f));
    ImGui::DockBuilderSetNodeSize(root, ImVec2(900.0f, 600.0f));
    ImGuiID right = 0;
    const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
        0.4f, nullptr, &right);
    ImGuiID bottom_left = 0;
    const ImGuiID top_left = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up,
        0.5f, nullptr, &bottom_left);
    ImGuiID rest = 0;
    const ImGuiID column = ImGui::DockBuilderSplitNode(right, ImGuiDir_Left,
        0.4f, nullptr, &rest);
    ImGuiID bottom_right = 0;
    const ImGuiID top_right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Up,
        0.5f, nullptr, &bottom_right);
    ImGui::DockBuilderDockWindow("###g1", top_left);
    ImGui::DockBuilderDockWindow("###g2", bottom_left);
    ImGui::DockBuilderDockWindow("###g3", column);
    ImGui::DockBuilderDockWindow("###g4", top_right);
    ImGui::DockBuilderDockWindow("###g5", bottom_right);
    ImGui::DockBuilderFinish(root);
}

void add_grid(open_desk& d) {
    for (const char* id : grid_ids)
        add_card(d, id, id);
}

/** @brief Where each grid card sits, by id. */
[[nodiscard]] std::vector<ImRect> grid_rects(const open_desk& d) {
    std::vector<ImRect> rects;
    for (const char* id : grid_ids) {
        const ImGuiWindow* w = d.window(std::format("###{}", id).c_str());
        REQUIRE(w != nullptr);
        REQUIRE(w->DockIsActive);
        rects.push_back(ImRect(w->Pos,
            ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y)));
    }
    return rects;
}

}

TEST_CASE("five docked cards each fill a tile and none overlaps another",
    "[mini_plot][grid]") {
    open_desk d;
    add_grid(d);
    d.start();
    ImGui::NewFrame();
    dock_grid();
    d.workspace.render(d.input);
    ImGui::Render();
    d.settle();

    const auto rects = grid_rects(d);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const ImGuiWindow* w =
            d.window(std::format("###{}", grid_ids[i]).c_str());
        CHECK(rects[i].GetWidth() > 100.0f);
        CHECK(rects[i].GetHeight() > 100.0f);
        CHECK_FALSE(w->DrawList->VtxBuffer.empty());
        CHECK(w->DockNode->IsHiddenTabBar());
        CHECK(w->DockNode->Windows.Size == 1);
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            INFO(grid_ids[i] << " against " << grid_ids[j]);
            const ImRect shared(
                ImVec2(std::max(rects[i].Min.x, rects[j].Min.x),
                    std::max(rects[i].Min.y, rects[j].Min.y)),
                ImVec2(std::min(rects[i].Max.x, rects[j].Max.x),
                    std::min(rects[i].Max.y, rects[j].Max.y)));
            CHECK((shared.GetWidth() <= 1.0f || shared.GetHeight() <= 1.0f));
        }
    }
}

TEST_CASE("docked cards keep their places after a restart through the ini file",
    "[mini_plot][grid]") {
    std::string saved;
    std::vector<ImRect> before;
    {
        open_desk d;
        add_grid(d);
        d.start();
        ImGui::NewFrame();
        dock_grid();
        d.workspace.render(d.input);
        ImGui::Render();
        d.settle();
        before = grid_rects(d);
        saved = ImGui::SaveIniSettingsToMemory();
    }

    open_desk d;
    add_grid(d);
    d.settings = saved;
    d.start();

    const auto after = grid_rects(d);
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        INFO(grid_ids[i]);
        CHECK(after[i].Min.x == Catch::Approx(before[i].Min.x).margin(1.0));
        CHECK(after[i].Min.y == Catch::Approx(before[i].Min.y).margin(1.0));
        CHECK(after[i].Max.x == Catch::Approx(before[i].Max.x).margin(1.0));
        CHECK(after[i].Max.y == Catch::Approx(before[i].Max.y).margin(1.0));
        CHECK(d.window(std::format("###{}", grid_ids[i]).c_str())
                ->DockNode->IsHiddenTabBar());
    }
}

TEST_CASE("a card dropped on the middle of another card splits instead of tabbing",
    "[mini_plot][grid]") {
    open_desk d;
    add_card(d, "m", "s-3");
    add_card(d, "n", "s-4");
    add_card(d, "f", "s-5");
    d.start();
    ImGui::NewFrame();
    {
        const ImGuiID root = ImGui::GetID("test-dock");
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodePos(root, ImVec2(20.0f, 300.0f));
        ImGui::DockBuilderSetNodeSize(root, ImVec2(900.0f, 400.0f));
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
    const ImGuiWindow* target = d.window("###m");
    REQUIRE(target != nullptr);
    REQUIRE(target->DockIsActive);
    const ImVec2 middle(target->Pos.x + target->Size.x * 0.5f,
        target->Pos.y + target->Size.y * 0.5f);
    const ImGuiWindow* dropped = d.window("###f");
    REQUIRE(dropped != nullptr);
    const ImVec2 title(dropped->Pos.x + 60.0f, dropped->Pos.y + 8.0f);

    d.drag(title, middle);
    d.settle();

    const ImGuiWindow* m = d.window("###m");
    const ImGuiWindow* f = d.window("###f");
    REQUIRE(m != nullptr);
    REQUIRE(f != nullptr);
    CHECK(m->DockNode->Windows.Size == 1);
    CHECK(f->DockNode != m->DockNode);
}

TEST_CASE("a card dropped on the empty desk centre does not fill the desk",
    "[mini_plot][grid]") {
    open_desk d;
    add_card(d, "f", "s-5");
    d.start();
    const ImGuiWindow* dropped = d.window("###f");
    REQUIRE(dropped != nullptr);
    const ImVec2 title(dropped->Pos.x + 60.0f, dropped->Pos.y + 8.0f);

    d.drag(title, ImVec2(640.0f, 400.0f));
    d.settle();

    const ImGuiWindow* f = d.window("###f");
    REQUIRE(f != nullptr);
    CHECK_FALSE(f->DockIsActive);
}

TEST_CASE("a docked card is closed from its context menu",
    "[mini_plot][grid]") {
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
    const ImVec2 at(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);

    d.hover(at);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    d.frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    d.frame();
    d.frame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    REQUIRE(g.OpenPopupStack.Size == 1);
    const ImGuiWindow* menu = g.OpenPopupStack[0].Window;
    REQUIRE(menu != nullptr);
    d.click(ImVec2(menu->Pos.x + menu->WindowPadding.x + 12.0f,
        menu->Pos.y + menu->WindowPadding.y + ImGui::GetFontSize() * 0.5f));

    bool closed = false;
    for (auto& c : d.desk.commands.drain()) {
        if (c.kind == command_kind::close_window)
            closed = closed || c.window_id == "m";
    }
    CHECK(closed);
}
