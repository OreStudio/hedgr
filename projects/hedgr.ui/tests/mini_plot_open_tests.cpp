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
        /*
         * The top bar insets the work area a frame after it first draws, and
         * the tests place cards from WorkPos before start(), so the inset is
         * established first, on an empty desk.
         */
        settle();
    }

    void place(const std::string& id, float width, float height) {
        settings += std::format("[Window][###{}]\nPos={},40\nSize={},{}\n\n",
            id, next_x, width, height);
        next_x += width + 40.0f;
    }

    void place_at(const std::string& id, float x, float y,
        const char* extra = "") {
        settings += std::format(
            "[Window][###{}]\nPos={},{}\nSize=240,180\n{}\n", id, x, y,
            extra);
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

    d.drag(at, ImVec2(at.x + 300.0f, at.y + 40.0f));
    CHECK(d.take().empty());
    CHECK(d.window("###m")->Pos.x > left + 100.0f);
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

    d.drag(body, ImVec2(body.x + 300.0f, body.y + 200.0f));
    d.settle();
    const ImGuiWindow* after = d.window("###m");
    REQUIRE(after != nullptr);
    CHECK(after->Pos.x > before.x + 100.0f);
    CHECK(after->Pos.y > before.y + 100.0f);
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


namespace {

constexpr float pitch_x = 244.0f;
constexpr float pitch_y = 184.0f;

[[nodiscard]] ImVec2 centre_of(const ImGuiWindow* w) {
    return ImVec2(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);
}

}

TEST_CASE("a card is not dockable and not resizable", "[mini_plot][snap]") {
    open_desk d;
    add_card(d, "m", "s-3");
    add_chart(d, "c", "s-3");
    d.start();
    const ImGuiWindow* card = d.window("###m");
    const ImGuiWindow* chart = d.window("###c");
    REQUIRE(card != nullptr);
    REQUIRE(chart != nullptr);
    CHECK((card->Flags & ImGuiWindowFlags_NoDocking) != 0);
    CHECK((card->Flags & ImGuiWindowFlags_NoResize) != 0);
    CHECK((chart->Flags & ImGuiWindowFlags_NoDocking) == 0);
    CHECK((chart->Flags & ImGuiWindowFlags_NoResize) == 0);
}

TEST_CASE("a card dragged onto a dock target stays floating",
    "[mini_plot][snap]") {
    open_desk d;
    add_chart(d, "c", "s-3");
    add_card(d, "m", "s-3");
    d.start();
    REQUIRE(d.click_chart_icon("###c", "dock", hedgr::ui::icon::panel));
    d.settle();
    REQUIRE(d.window("###c")->DockIsActive);
    const ImGuiWindow* chart = d.window("###c");

    /* ImGui docks by a drag that starts in the window's top strip. */
    const auto grab = [&] {
        const ImGuiWindow* card = d.window("###m");
        return ImVec2(card->Pos.x + 60.0f, card->Pos.y + 8.0f);
    };
    d.drag(grab(), centre_of(chart));
    d.settle();
    REQUIRE(d.window("###m") != nullptr);
    CHECK_FALSE(d.window("###m")->DockIsActive);

    d.drag(grab(), ImVec2(chart->Pos.x + 4.0f, 400.0f));
    d.settle();
    CHECK_FALSE(d.window("###m")->DockIsActive);
    CHECK(d.window("###c")->DockNode->Windows.Size == 1);
}

TEST_CASE("a card keeps its size when its corner is dragged and when a saved size differs",
    "[mini_plot][snap]") {
    open_desk d;
    d.settings += "[Window][###m]\nPos=20,40\nSize=400,300\n\n";
    static_cast<void>(d.desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD",
            .series_id = "s-3"}, "m"));
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    CHECK(w->Size.x == Catch::Approx(240.0f));
    CHECK(w->Size.y == Catch::Approx(180.0f));

    const ImVec2 corner(w->Pos.x + w->Size.x - 2.0f,
        w->Pos.y + w->Size.y - 2.0f);
    d.drag(corner, ImVec2(corner.x + 150.0f, corner.y + 120.0f));
    d.settle();
    CHECK(d.window("###m")->Size.x == Catch::Approx(240.0f));
    CHECK(d.window("###m")->Size.y == Catch::Approx(180.0f));
}

TEST_CASE("a card dropped after a drag snaps to the grid pitch",
    "[mini_plot][snap]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImVec2 origin = ImGui::GetMainViewport()->WorkPos;
    const ImVec2 grab = centre_of(d.window("###m"));

    d.drag(grab, ImVec2(grab.x + 300.0f, grab.y + 200.0f));
    d.settle();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    CHECK(w->Pos.x == Catch::Approx(origin.x + pitch_x));
    CHECK(w->Pos.y == Catch::Approx(origin.y + pitch_y));
}

TEST_CASE("a card dropped on an occupied cell goes to the nearest free cell",
    "[mini_plot][snap]") {
    open_desk d;
    const ImVec2 origin = ImGui::GetMainViewport()->WorkPos;
    d.place_at("m", origin.x + 20.0f, origin.y + 40.0f);
    d.place_at("n", origin.x + pitch_x, origin.y);
    static_cast<void>(d.desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD",
            .series_id = "s-3"}, "m"));
    static_cast<void>(d.desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX EUR/USD",
            .series_id = "s-4"}, "n"));
    d.start();
    const ImVec2 grab = centre_of(d.window("###m"));

    /* Lands at (260,50): cell (1,0) is n's, and (1,1) is the nearest free. */
    d.drag(grab, ImVec2(grab.x + 240.0f, grab.y + 10.0f));
    d.settle();
    CHECK(d.window("###m")->Pos.x == Catch::Approx(origin.x + pitch_x));
    CHECK(d.window("###m")->Pos.y == Catch::Approx(origin.y + pitch_y));
    CHECK(d.window("###n")->Pos.x == Catch::Approx(origin.x + pitch_x));
    CHECK(d.window("###n")->Pos.y == Catch::Approx(origin.y));
}

TEST_CASE("new cards open in the first free cells in order",
    "[mini_plot][snap]") {
    open_desk d;
    const ImVec2 origin = ImGui::GetMainViewport()->WorkPos;
    /* A restored card holds the second cell, and the new ones skip it. */
    d.place_at("held", origin.x + pitch_x, origin.y);
    for (const char* id : {"held", "a", "b"})
        static_cast<void>(d.desk.open(window_kind::mini_plot,
            model::domain::mini_plot_state{.label = "FX GBP/USD",
                .series_id = id}, id));
    d.start();

    CHECK(d.window("###held")->Pos.x == Catch::Approx(origin.x + pitch_x));
    CHECK(d.window("###a")->Pos.x == Catch::Approx(origin.x));
    CHECK(d.window("###a")->Pos.y == Catch::Approx(origin.y));
    CHECK(d.window("###b")->Pos.x == Catch::Approx(origin.x + 2.0f * pitch_x));
    CHECK(d.window("###b")->Pos.y == Catch::Approx(origin.y));
}

TEST_CASE("a card's close control shows on hover and closes without opening the chart",
    "[mini_plot][snap]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 close(w->Pos.x + w->Size.x - 14.0f, w->Pos.y + 14.0f);

    d.hover(ImVec2(-50.0f, -50.0f));
    const int idle = d.window("###m")->DrawList->VtxBuffer.Size;
    d.hover(ImVec2(w->Pos.x + 5.0f, w->Pos.y + w->Size.y - 5.0f));
    d.hover(ImVec2(w->Pos.x + 6.0f, w->Pos.y + w->Size.y - 5.0f));
    const int over_body = d.window("###m")->DrawList->VtxBuffer.Size;
    CHECK(over_body > idle);
    d.hover(close);
    CHECK(d.window("###m")->DrawList->VtxBuffer.Size > over_body);
    d.hover(ImVec2(-50.0f, -50.0f));
    CHECK(d.window("###m")->DrawList->VtxBuffer.Size == idle);

    d.click(close);
    bool closed = false;
    bool opened = false;
    for (auto& c : d.desk.commands.drain()) {
        closed = closed
            || (c.kind == command_kind::close_window && c.window_id == "m");
        opened = opened || c.kind == command_kind::open_window;
    }
    CHECK(closed);
    CHECK_FALSE(opened);
}

TEST_CASE("pressing a card's close control does not start a drag",
    "[mini_plot][snap]") {
    open_desk d;
    add_card(d, "m", "s-3");
    d.start();
    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    const ImVec2 before = w->Pos;
    const ImVec2 close(w->Pos.x + w->Size.x - 14.0f, w->Pos.y + 14.0f);

    d.drag(close, ImVec2(close.x - 200.0f, close.y + 100.0f));
    d.settle();
    CHECK(d.window("###m")->Pos.x == Catch::Approx(before.x));
    CHECK(d.window("###m")->Pos.y == Catch::Approx(before.y));
    for (auto& c : d.desk.commands.drain())
        CHECK(c.kind != command_kind::close_window);
}

TEST_CASE("a saved layout that docked a card brings it back floating at the fixed size",
    "[mini_plot][snap]") {
    /*
     * A window of the same id, docked as a chart can be, writes the ini an old
     * run left behind for a docked card.
     */
    std::string saved;
    {
        open_desk d;
        add_chart(d, "m", "s-3");
        d.start();
        REQUIRE(d.click_chart_icon("###m", "dock", hedgr::ui::icon::panel));
        d.settle();
        REQUIRE(d.window("###m")->DockIsActive);
        saved = ImGui::SaveIniSettingsToMemory();
        REQUIRE(saved.find("DockId=") != std::string::npos);
    }

    open_desk d;
    static_cast<void>(d.desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD",
            .series_id = "s-3"}, "m"));
    d.settings = saved;
    d.start();

    const ImGuiWindow* w = d.window("###m");
    REQUIRE(w != nullptr);
    CHECK_FALSE(w->DockIsActive);
    CHECK(w->DockNode == nullptr);
    CHECK(w->Size.x == Catch::Approx(240.0f));
    CHECK(w->Size.y == Catch::Approx(180.0f));
}
