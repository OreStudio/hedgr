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
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/toolbar.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "hedgr.ui/widget/toolbar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;

constexpr const char* toolbar_window = "###hedgr-toolbar";

ImU32 u32(ImVec4 value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

/** @brief The toolbar drawn alone, over a desk, with the pointer under test control. */
struct toolbar_harness final {
    headless_imgui imgui;
    desk desk;
    render::render_input input;
    render::toolbar_result result;
    bool docked = false;
    bool with_menu_bar = false;
    widget::menu_bar_state bar_state;

    toolbar_harness() : input(desk.input()) {
        theme::apply_theme();
        model::domain::instance row;
        row.id = "i";
        row.name = "inst";
        static_cast<void>(desk.instances.upsert(row));
    }

    void frame() {
        result = {};
        ImGui::NewFrame();
        if (with_menu_bar) {
            render::menu_bar_result bar;
            render::draw_menu_bar(input, {}, docked, bar_state, bar);
        }
        render::draw_toolbar(input, docked, result);
        ImGui::Render();
    }

    void settle() {
        for (int i = 0; i < 4; ++i)
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

    [[nodiscard]] static ImGuiWindow* window() {
        return ImGui::FindWindowByName(toolbar_window);
    }

    [[nodiscard]] ImVec2 button_centre(std::size_t index) {
        const auto rect = widget::toolbar_button_rect(window()->Pos, index);
        return ImVec2((rect.min.x + rect.max.x) * 0.5f,
            (rect.min.y + rect.max.y) * 0.5f);
    }
};

const ImVec2 area_pos(0.0f, 46.0f);
const ImVec2 area_size(1000.0f, 600.0f);
const ImVec2 capsule(200.0f, 44.0f);

}

TEST_CASE("the clamp keeps the capsule inside the work area past every edge",
    "[toolbar]") {
    const float m = theme::toolbar_edge_margin;
    const auto at = [&](float x, float y) {
        return widget::toolbar_clamp(ImVec2(x, y), capsule, area_pos, area_size);
    };
    CHECK(at(-500.0f, 300.0f).x == Catch::Approx(area_pos.x + m));
    CHECK(at(5000.0f, 300.0f).x
        == Catch::Approx(area_pos.x + area_size.x - capsule.x - m));
    CHECK(at(300.0f, 9000.0f).y
        == Catch::Approx(area_pos.y + area_size.y - capsule.y - m));
    CHECK(at(300.0f, -500.0f).y == Catch::Approx(area_pos.y));
    /* A position already inside is left alone. */
    CHECK(at(300.0f, 200.0f).x == Catch::Approx(300.0f));
    CHECK(at(300.0f, 200.0f).y == Catch::Approx(200.0f));
}

TEST_CASE("the clamp never lets the capsule above the work area, which is below "
          "the menu bar",
    "[toolbar]") {
    const ImVec2 clamped = widget::toolbar_clamp(ImVec2(300.0f, 0.0f), capsule,
        area_pos, area_size);
    CHECK(clamped.y >= area_pos.y);
}

TEST_CASE("a work area smaller than the capsule pins it to the top left",
    "[toolbar]") {
    const ImVec2 clamped = widget::toolbar_clamp(ImVec2(300.0f, 300.0f), capsule,
        area_pos, ImVec2(50.0f, 20.0f));
    CHECK(clamped.x == Catch::Approx(area_pos.x + theme::toolbar_edge_margin));
    CHECK(clamped.y == Catch::Approx(area_pos.y));
}

TEST_CASE("the default position is centred just under the menu bar", "[toolbar]") {
    const ImVec2 size = widget::toolbar_size();
    const ImVec2 at = widget::toolbar_default_pos(size, area_pos, area_size);
    CHECK(at.x + size.x * 0.5f
        == Catch::Approx(area_pos.x + area_size.x * 0.5f).margin(1.0));
    CHECK(at.y == Catch::Approx(area_pos.y + theme::toolbar_top_gap));
}

TEST_CASE("the buttons tile the capsule in a row with the gap between them",
    "[toolbar]") {
    const ImVec2 origin(100.0f, 50.0f);
    const ImVec2 size = widget::toolbar_size();
    const auto grip = widget::toolbar_grip_rect(origin);
    CHECK(grip.min.x == Catch::Approx(origin.x + theme::toolbar_pad_x));
    CHECK(grip.max.y - grip.min.y == Catch::Approx(theme::toolbar_button_side));

    float previous_right = grip.max.x;
    for (std::size_t i = 0; i < widget::toolbar_action_count; ++i) {
        const auto rect = widget::toolbar_button_rect(origin, i);
        CHECK(rect.min.x - previous_right == Catch::Approx(theme::toolbar_gap));
        CHECK(rect.max.x - rect.min.x == Catch::Approx(theme::toolbar_button_side));
        CHECK(rect.min.y == Catch::Approx(origin.y + theme::toolbar_pad_y));
        previous_right = rect.max.x;
    }
    /* The last button ends one pad short of the capsule's right end. */
    CHECK(origin.x + size.x - previous_right
        == Catch::Approx(theme::toolbar_pad_x));
    CHECK(size.y == Catch::Approx(
        theme::toolbar_button_side + 2.0f * theme::toolbar_pad_y));
}

TEST_CASE("Undock all is enabled only while something is docked and the "
          "windows only with a session",
    "[toolbar]") {
    using action = widget::toolbar_action;
    CHECK_FALSE(widget::toolbar_enabled(action::undock_all, {.docked = false}));
    CHECK(widget::toolbar_enabled(action::undock_all, {.docked = true}));
    CHECK_FALSE(widget::toolbar_enabled(action::series_browser, {.docked = true}));
    CHECK_FALSE(widget::toolbar_enabled(action::accounts, {.docked = true}));
    CHECK(widget::toolbar_enabled(action::series_browser, {.session = true}));
    CHECK(widget::toolbar_enabled(action::accounts, {.session = true}));
}

TEST_CASE("each button issues the command its menu row does", "[toolbar]") {
    toolbar_harness h;
    h.docked = true;
    h.settle();
    REQUIRE(toolbar_harness::window() != nullptr);
    static_cast<void>(h.desk.commands.drain());

    h.click(h.button_centre(0));
    auto commands = h.desk.commands.drain();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::open_window);
    CHECK(commands[0].new_window_kind
        == model::domain::window_kind::series_browser);
    CHECK(commands[0].instance_id == "i");
    CHECK(commands[0].account_id == "a");

    h.click(h.button_centre(1));
    commands = h.desk.commands.drain();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::open_window);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::accounts);
    CHECK(commands[0].instance_id == "i");

    h.click(h.button_centre(2));
    CHECK(h.result.undock_all);
    CHECK(h.desk.commands.drain().empty());
}

TEST_CASE("Undock all does nothing while nothing is docked", "[toolbar]") {
    toolbar_harness h;
    h.settle();
    const ImVec2 at = h.button_centre(2);
    h.hover(at);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    h.frame();
    bool asked = h.result.undock_all;
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    h.frame();
    asked = asked || h.result.undock_all;
    CHECK_FALSE(asked);
}

TEST_CASE("the window buttons do nothing without a signed-in session",
    "[toolbar]") {
    toolbar_harness h;
    h.desk.sessions.set_status(h.desk.session,
        model::domain::session_status::failed);
    h.settle();
    static_cast<void>(h.desk.commands.drain());
    h.click(h.button_centre(0));
    h.click(h.button_centre(1));
    CHECK(h.desk.commands.drain().empty());
}

TEST_CASE("nothing is drawn while the store is locked", "[toolbar]") {
    toolbar_harness h;
    h.input.store_lock = model::domain::store_lock::locked;
    h.settle();
    CHECK(toolbar_harness::window() == nullptr);

    h.input.store_lock = model::domain::store_lock::unlocked;
    h.settle();
    CHECK(toolbar_harness::window() != nullptr);
}

TEST_CASE("the toolbar is hidden while its preference is off", "[toolbar]") {
    toolbar_harness h;
    h.desk.market_view.toolbar_visible = false;
    h.settle();
    CHECK((toolbar_harness::window() == nullptr
        || !toolbar_harness::window()->WasActive));
    h.desk.market_view.toolbar_visible = true;
    h.settle();
    REQUIRE(toolbar_harness::window() != nullptr);
    CHECK(toolbar_harness::window()->WasActive);
}

TEST_CASE("the window is not dockable, has no title bar and fits its content",
    "[toolbar]") {
    toolbar_harness h;
    h.settle();
    const ImGuiWindow* w = toolbar_harness::window();
    REQUIRE(w != nullptr);
    CHECK((w->Flags & ImGuiWindowFlags_NoDocking) != 0);
    CHECK((w->Flags & ImGuiWindowFlags_NoTitleBar) != 0);
    CHECK((w->Flags & ImGuiWindowFlags_NoResize) != 0);
    CHECK((w->Flags & ImGuiWindowFlags_NoScrollbar) != 0);
    CHECK(w->Size.x == Catch::Approx(widget::toolbar_size().x));
    CHECK(w->Size.y == Catch::Approx(widget::toolbar_size().y));
    CHECK(w->DockNode == nullptr);
}

TEST_CASE("a position outside the work area is pulled back inside, below the "
          "menu bar",
    "[toolbar]") {
    toolbar_harness h;
    h.with_menu_bar = true;
    h.settle();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    REQUIRE(viewport->WorkPos.y >= theme::menu_bar_extent - 1.0f);

    for (const ImVec2 outside : {ImVec2(-900.0f, -900.0f),
             ImVec2(9000.0f, 9000.0f), ImVec2(-900.0f, 9000.0f)}) {
        ImGui::SetWindowPos(toolbar_window, outside);
        h.settle();
        const ImGuiWindow* w = toolbar_harness::window();
        const ImVec2 want = widget::toolbar_clamp(outside, widget::toolbar_size(),
            viewport->WorkPos, viewport->WorkSize);
        CHECK(w->Pos.x == Catch::Approx(want.x));
        CHECK(w->Pos.y == Catch::Approx(want.y));
        CHECK(w->Pos.y >= viewport->WorkPos.y);
    }
}

TEST_CASE("dragging the grip moves the capsule and stops at the edge",
    "[toolbar]") {
    toolbar_harness h;
    h.settle();
    const ImVec2 start = toolbar_harness::window()->Pos;
    const auto grip = widget::toolbar_grip_rect(start);
    const ImVec2 hold((grip.min.x + grip.max.x) * 0.5f,
        (grip.min.y + grip.max.y) * 0.5f);
    h.hover(hold);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    h.frame();
    h.hover(ImVec2(hold.x + 60.0f, hold.y + 40.0f));
    h.frame();
    CHECK(toolbar_harness::window()->Pos.x == Catch::Approx(start.x + 60.0f));
    CHECK(toolbar_harness::window()->Pos.y == Catch::Approx(start.y + 40.0f));

    h.hover(ImVec2(9000.0f, 9000.0f));
    h.frame();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 limit = widget::toolbar_clamp(ImVec2(9000.0f, 9000.0f),
        widget::toolbar_size(), viewport->WorkPos, viewport->WorkSize);
    CHECK(toolbar_harness::window()->Pos.x == Catch::Approx(limit.x));
    CHECK(toolbar_harness::window()->Pos.y == Catch::Approx(limit.y));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    h.frame();
}

TEST_CASE("the position is kept in the ini and restored", "[toolbar]") {
    std::string saved;
    {
        toolbar_harness h;
        h.settle();
        ImGui::SetWindowPos(toolbar_window, ImVec2(333.0f, 222.0f));
        h.settle();
        ImGui::MarkIniSettingsDirty();
        saved = ImGui::SaveIniSettingsToMemory();
    }
    toolbar_harness h;
    ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
    h.settle();
    CHECK(toolbar_harness::window()->Pos.x == Catch::Approx(333.0f));
    CHECK(toolbar_harness::window()->Pos.y == Catch::Approx(222.0f));
}

TEST_CASE("the capsule is a pill: corner radius half its height, purple outline",
    "[toolbar][look]") {
    toolbar_harness h;
    h.settle();
    const ImGuiWindow* w = toolbar_harness::window();
    REQUIRE(w != nullptr);
    const ImVec2 size = widget::toolbar_size();
    const float radius = size.y * 0.5f;
    const auto& verts = w->DrawList->VtxBuffer;
    const ImU32 outline = u32(theme::toolbar_outline());
    const ImU32 fill = u32(theme::toolbar_fill());

    CHECK(outline == u32(theme::chart_tag_colour()));
    CHECK(theme::toolbar_outline_width == Catch::Approx(1.5f));
    CHECK(((fill >> IM_COL32_A_SHIFT) & 0xffu) < 0xffu);

    const auto any = [&](ImU32 col, auto&& where) {
        return std::ranges::any_of(verts, [&](const ImDrawVert& v) {
            return v.col == col && where(v.pos);
        });
    };
    /* The fill's flat top starts one radius in, so the bounding corner is cut. */
    CHECK_FALSE(any(fill, [&](ImVec2 p) {
        return p.x < w->Pos.x + radius - 1.0f && p.y < w->Pos.y + 1.0f;
    }));
    CHECK(any(fill, [&](ImVec2 p) {
        return p.x == Catch::Approx(w->Pos.x).margin(1.0)
            && p.y > w->Pos.y + radius - 2.0f
            && p.y < w->Pos.y + radius + 2.0f;
    }));
    CHECK(any(fill, [&](ImVec2 p) {
        return p.x == Catch::Approx(w->Pos.x + size.x).margin(1.0);
    }));
    CHECK(any(outline, [&](ImVec2 p) {
        return p.x > w->Pos.x + radius && p.y < w->Pos.y + 3.0f;
    }));
}
