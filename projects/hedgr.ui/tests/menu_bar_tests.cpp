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
#include <map>
#include <optional>
#include <string>
#include <vector>

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
constexpr const char* about_window = "###hedgr-about";

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
    bool docked = false;
    std::string svg = "<svg xmlns='http://www.w3.org/2000/svg'/>";
    std::map<std::string, ImVec2> centres;
    bool scanned = false;

    bar_harness() : input(desk.input()) {
        theme::apply_theme();
    }

    void frame() {
        result = {};
        ImGui::NewFrame();
        render::draw_menu_bar(input, svg, docked, state, result);
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

    /**
     * @brief Where a top-level label is, found by sweeping the pointer along the
     * bar and watching which item is hovered, or none when it is not drawn.
     */
    [[nodiscard]] std::optional<ImVec2> label(const char* name) {
        if (!scanned) {
            scanned = true;
            settle();
            std::map<std::string, std::pair<float, float>> spans;
            const ImGuiWindow* bar = bar_harness::bar();
            for (float x = 2.0f; x < 600.0f; x += 2.0f) {
                hover(ImVec2(x, theme::menu_bar_height * 0.5f));
                for (const char* n : {"File", "View", "Tools", "Help"}) {
                    const ImGuiID id = ImHashStr("##label", 0,
                        ImHashStr(n, 0, bar->ID));
                    if (GImGui->HoveredId != id)
                        continue;
                    auto& span = spans.try_emplace(n, x, x).first->second;
                    span.second = x;
                }
            }
            for (const auto& [n, span] : spans)
                centres[n] = ImVec2((span.first + span.second) * 0.5f,
                    theme::menu_bar_height * 0.5f);
        }
        const auto found = centres.find(name);
        if (found == centres.end())
            return std::nullopt;
        return found->second;
    }

    /** @brief Opens the menu named @p name by a click on its label. */
    void open_menu(const char* name) {
        const auto at = label(name);
        REQUIRE(at.has_value());
        click(*at);
        settle();
    }

    /** @brief The centre of the n-th row of the open menu, from the top. */
    [[nodiscard]] static ImVec2 row_centre(int n) {
        REQUIRE(GImGui->OpenPopupStack.Size > 0);
        const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
        REQUIRE(popup != nullptr);
        const float row = ImGui::GetTextLineHeight()
            + 2.0f * theme::menu_row_padding_y;
        return ImVec2(popup->Pos.x + theme::menu_padding + 30.0f,
            popup->Pos.y + theme::menu_padding
                + (row + theme::menu_row_gap) * static_cast<float>(n) + row * 0.5f);
    }

    [[nodiscard]] static bool popup_open(const char* name = "File") {
        return popup_open_in(bar_window, name);
    }

    [[nodiscard]] static const ImGuiWindow* bar() {
        return ImGui::FindWindowByName(bar_window);
    }

    /** @brief The commands the frames so far pushed. */
    [[nodiscard]] std::vector<model::command::command> commands() {
        return desk.commands.drain();
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
    h.open_menu("File");
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

TEST_CASE("the bar is shaded across and down, with a hairline along its bottom edge",
    "[menu_bar][look]") {
    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    const auto& verts = bar->DrawList->VtxBuffer;
    const float left = bar->Pos.x;
    const float right = bar->Pos.x + bar->Size.x;
    const float top = bar->Pos.y;
    const float bottom = bar->Pos.y + bar->Size.y;
    const auto corner = [&](float x, float y, ImU32 colour) {
        return std::ranges::any_of(verts, [&](const ImDrawVert& v) {
            return v.col == colour && v.pos.x == Catch::Approx(x)
                && v.pos.y == Catch::Approx(y);
        });
    };
    /* The wash runs down, clear at the top, so the across tint has no top edge. */
    CHECK(corner(left, top, theme::menu_bar_gradient_left));
    CHECK(corner(right, top, theme::menu_bar_gradient_right));
    CHECK(corner(left, top, theme::menu_bar_shade_top));
    CHECK(corner(left, bottom, theme::menu_bar_shade_bottom));
    CHECK(theme::menu_bar_gradient_left != theme::menu_bar_gradient_right);
    CHECK(theme::menu_bar_shade_top != theme::menu_bar_shade_bottom);
    CHECK(std::ranges::any_of(verts, [&](const ImDrawVert& v) {
        return v.col == u32(theme::menu_border()) && v.pos.y > bottom - 1.5f
            && v.pos.y <= bottom;
    }));
}

TEST_CASE("the logo is drawn at the bar's scale as a plain mark that never lights",
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
    CHECK(tinted(ImGui::GetColorU32(ImGuiCol_Text)));
    h.hover(bar_harness::logo_centre());
    h.settle();
    CHECK(tinted(ImGui::GetColorU32(ImGuiCol_Text)));
    CHECK_FALSE(tinted(u32(theme::dim_text())));
}

TEST_CASE("the logo is not a menu: a click on it opens nothing", "[menu_bar]") {
    bar_harness h;
    h.settle();
    h.click(bar_harness::logo_centre());
    h.settle();
    CHECK(h.state.open == menu_bar_state::none);
    CHECK(GImGui->OpenPopupStack.Size == 0);
    CHECK(h.commands().empty());
}

TEST_CASE("a bar with no logo document still draws its menus", "[menu_bar]") {
    bar_harness h;
    h.svg.clear();
    h.settle();
    CHECK(h.desk.textures.svg_gets == 0);
    h.open_menu("File");
    CHECK(bar_harness::popup_open());
}

TEST_CASE("clicking a label opens its menu and clicking it again closes it",
    "[menu_bar]") {
    bar_harness h;
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    const auto file = h.label("File");
    REQUIRE(file.has_value());
    h.click(*file);
    h.settle();
    CHECK(bar_harness::popup_open());
    CHECK(h.state.open == 0);
    h.click(*file);
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    CHECK(h.state.open == menu_bar_state::none);
}

TEST_CASE("clicking away closes the open menu", "[menu_bar]") {
    bar_harness h;
    h.open_menu("File");
    REQUIRE(bar_harness::popup_open());
    h.click(ImVec2(900.0f, 600.0f));
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
    CHECK(h.state.open == menu_bar_state::none);
}

TEST_CASE("the bar leads with File, View, Tools and Help in that order",
    "[menu_bar]") {
    bar_harness h;
    const auto file = h.label("File");
    const auto view = h.label("View");
    const auto tools = h.label("Tools");
    const auto help = h.label("Help");
    REQUIRE((file && view && tools && help));
    CHECK(file->x > theme::menu_bar_height);
    CHECK(file->x < view->x);
    CHECK(view->x < tools->x);
    CHECK(tools->x < help->x);
}

TEST_CASE("hovering across the real labels moves the open menu", "[menu_bar]") {
    bar_harness h;
    h.open_menu("File");
    REQUIRE(bar_harness::popup_open("File"));
    h.hover(*h.label("Tools"));
    h.settle();
    CHECK(h.state.open == 2);
    CHECK(bar_harness::popup_open("Tools"));
    CHECK_FALSE(bar_harness::popup_open("File"));
    h.hover(*h.label("Help"));
    h.settle();
    CHECK(bar_harness::popup_open("Help"));
    CHECK_FALSE(bar_harness::popup_open("Tools"));
}

TEST_CASE("hovering a real label opens nothing while no menu is open",
    "[menu_bar]") {
    bar_harness h;
    h.settle();
    h.hover(*h.label("View"));
    h.settle();
    CHECK(h.state.open == menu_bar_state::none);
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("FILE: Instances first, then the lock row, then Quit", "[menu_bar][file]") {
    bar_harness h;
    h.open_menu("File");
    h.click(bar_harness::row_centre(0));
    CHECK(h.result.focus_instances);
    bool asked = false;
    for (auto& c : h.commands())
        asked = asked || (c.kind == model::command::command_kind::open_window
            && c.new_window_kind == model::domain::window_kind::instance_manager);
    CHECK(asked);
    h.settle();
    CHECK_FALSE(bar_harness::popup_open());
}

TEST_CASE("FILE: the next row locks an open store", "[menu_bar][file]") {
    bar_harness h;
    h.open_menu("File");
    h.click(bar_harness::row_centre(1));
    CHECK(h.result.lock_requested);
    CHECK_FALSE(h.result.quit_requested);
}

TEST_CASE("FILE: Quit asks to leave and nothing else", "[menu_bar][file]") {
    bar_harness h;
    h.open_menu("File");
    h.click(bar_harness::row_centre(2));
    CHECK(h.result.quit_requested);
    CHECK_FALSE(h.result.lock_requested);
    CHECK_FALSE(h.result.focus_instances);
    CHECK(h.commands().empty());
}

TEST_CASE("a locked store shows File alone, with the way in and Quit",
    "[menu_bar][lock]") {
    bar_harness h;
    h.input.store_lock = model::domain::store_lock::locked;
    h.settle();
    CHECK(h.label("File").has_value());
    CHECK_FALSE(h.label("View").has_value());
    CHECK_FALSE(h.label("Tools").has_value());
    CHECK_FALSE(h.label("Help").has_value());
    h.open_menu("File");
    REQUIRE(bar_harness::popup_open());
    h.click(bar_harness::row_centre(0));
    CHECK_FALSE(h.result.focus_instances);
    CHECK(h.result.unlock_requested);
    CHECK(h.commands().empty());

    h.open_menu("File");
    h.click(bar_harness::row_centre(1));
    CHECK(h.result.quit_requested);
}

TEST_CASE("a store with no password offers to create one", "[menu_bar][lock]") {
    bar_harness h;
    h.input.store_lock = model::domain::store_lock::absent;
    h.open_menu("File");
    h.click(bar_harness::row_centre(0));
    CHECK(h.result.unlock_requested);
    CHECK_FALSE(h.result.focus_instances);
}

TEST_CASE("VIEW: Undock all is chosen only while something is docked",
    "[menu_bar][view]") {
    bar_harness h;
    h.open_menu("View");
    REQUIRE(bar_harness::popup_open("View"));
    h.click(bar_harness::row_centre(0));
    CHECK_FALSE(h.result.undock_all);
    CHECK(bar_harness::popup_open("View"));

    h.click(ImVec2(900.0f, 600.0f));
    h.docked = true;
    h.open_menu("View");
    h.click(bar_harness::row_centre(0));
    CHECK(h.result.undock_all);
}

TEST_CASE("TOOLS: Series browser opens the browser of the signed-in session",
    "[menu_bar][tools]") {
    bar_harness h;
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(0));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::open_window);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::series_browser);
    CHECK(commands[0].instance_id == "i");
    CHECK(commands[0].account_id == "a");
}

TEST_CASE("TOOLS: Accounts opens the accounts of the session's instance",
    "[menu_bar][tools]") {
    bar_harness h;
    model::domain::instance row;
    row.id = "i";
    row.name = "inst";
    static_cast<void>(h.desk.instances.upsert(row));
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::accounts);
    CHECK(commands[0].instance_id == "i");
}

TEST_CASE("TOOLS: Command centre is there for an administrator's session only",
    "[menu_bar][tools]") {
    bar_harness h;
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(2));
    CHECK(h.commands().empty());

    h.desk.sessions.set_mode(h.desk.session,
        model::domain::session_mode::system_administration);
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(2));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].new_window_kind
        == model::domain::window_kind::command_centre);
}

TEST_CASE("TOOLS: with no signed-in session its rows are disabled",
    "[menu_bar][tools]") {
    bar_harness h;
    h.desk.sessions.set_status(h.desk.session,
        model::domain::session_status::disconnected);
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(0));
    h.click(bar_harness::row_centre(1));
    CHECK(h.commands().empty());
}

TEST_CASE("HELP: About opens the About window and its close control closes it",
    "[menu_bar][help]") {
    bar_harness h;
    h.settle();
    CHECK(ImGui::FindWindowByName(about_window) == nullptr);
    h.open_menu("Help");
    h.click(bar_harness::row_centre(0));
    h.settle();
    CHECK(h.state.about_open);
    const ImGuiWindow* about = ImGui::FindWindowByName(about_window);
    REQUIRE(about != nullptr);
    CHECK(about->Active);
    h.state.about_open = false;
    h.settle();
    CHECK_FALSE(ImGui::FindWindowByName(about_window)->Active);
}

TEST_CASE("the version is the project's own", "[menu_bar][help]") {
    const auto version = render::version_text();
    CHECK_FALSE(version.empty());
    CHECK(version != "unknown");
    CHECK(version.find('.') != std::string_view::npos);
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

TEST_CASE("through the workspace, File's Lock row locks and Quit asks to leave",
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
    /* File is the first label, just right of the logo. */
    const ImVec2 file(theme::menu_bar_height + 24.0f,
        theme::menu_bar_height * 0.5f);
    click(file);
    click(bar_harness::row_centre(1));
    bool locked = false;
    for (auto& c : desk.commands.drain())
        locked = locked || c.kind == model::command::command_kind::lock_store;
    CHECK(locked);
    CHECK_FALSE(workspace.exit_requested());

    click(file);
    click(bar_harness::row_centre(2));
    CHECK(workspace.exit_requested());
}
