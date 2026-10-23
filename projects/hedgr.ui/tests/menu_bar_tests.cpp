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
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "software_raster.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <map>
#include <set>
#include <span>
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
using hedgr::testing::software_raster;
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
        return ImVec2(theme::menu_bar_margin + theme::menu_bar_height * 0.5f,
            theme::menu_bar_extent * 0.5f);
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
                hover(ImVec2(x, theme::menu_bar_extent * 0.5f));
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
    CHECK(bar->Size.y == Catch::Approx(theme::menu_bar_extent));
    CHECK((bar->Flags & ImGuiWindowFlags_NoMove) != 0);
    CHECK((bar->Flags & ImGuiWindowFlags_NoDocking) != 0);
}

TEST_CASE("the bar's gradient runs along the measured stops and never down",
    "[menu_bar][look]") {
    const auto& stops = theme::menu_bar_stops;
    const std::span<const theme::gradient_stop> run(stops);
    const auto channel = [](ImU32 c, unsigned int shift) {
        return static_cast<int>((c >> shift) & 0xffu);
    };
    /* The stops are the reference's, at the bar's alpha. */
    CHECK(run.front().at == 0.0f);
    CHECK(run.back().at == 1.0f);
    CHECK(channel(run[0].colour, IM_COL32_R_SHIFT) == 0x3d);
    CHECK(channel(run[1].colour, IM_COL32_R_SHIFT) == 0x1a);
    CHECK(channel(run[2].colour, IM_COL32_B_SHIFT) == 0x26);
    CHECK(run[1].at == Catch::Approx(0.25f));
    /* Halfway to the knee is halfway between the first two colours. */
    const ImU32 mid = theme::stops_colour(run, 0.125f);
    CHECK(channel(mid, IM_COL32_R_SHIFT) == (0x3d + 0x1a + 1) / 2);
    /* Past the knee it stays near the flat page colour. */
    CHECK(channel(theme::stops_colour(run, 0.6f), IM_COL32_R_SHIFT) == 0x1b);

    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    const auto& verts = bar->DrawList->VtxBuffer;
    const float left = bar->Pos.x + theme::menu_bar_margin;
    const float right = bar->Pos.x + bar->Size.x - theme::menu_bar_margin;
    const float top = bar->Pos.y + theme::menu_bar_margin;
    const float bottom = top + theme::menu_bar_height;
    /*
     * Every vertex of the plate takes the stops' colour at its own x, whatever
     * its y, which is a gradient along the bar and no wash down it. The plate's
     * own vertices are the ones the logo and the text do not draw (they sample the
     * white pixel), and the hairline is told apart by its colour.
     */
    int plate_vertices = 0;
    float lowest_x = right;
    float highest_x = left;
    for (const ImDrawVert& v : verts) {
        if (v.pos.y < top - 1.0f || v.pos.y > bottom + 1.0f
            || v.pos.x < left - 1.0f || v.pos.x > right + 1.0f
            || (v.col >> IM_COL32_A_SHIFT) == 0
            || v.uv.x != ImGui::GetDrawListSharedData()->TexUvWhitePixel.x
            || v.uv.y != ImGui::GetDrawListSharedData()->TexUvWhitePixel.y
            || (v.col & ~IM_COL32_A_MASK)
                == (u32(theme::menu_bar_edge()) & ~IM_COL32_A_MASK))
            continue;
        const float t = std::clamp((v.pos.x - left) / (right - left), 0.0f, 1.0f);
        const ImU32 want = theme::stops_colour(run, t);
        INFO("x " << v.pos.x << " y " << v.pos.y);
        CHECK(channel(v.col, IM_COL32_R_SHIFT)
            == Catch::Approx(channel(want, IM_COL32_R_SHIFT)).margin(1.0));
        CHECK(channel(v.col, IM_COL32_G_SHIFT)
            == Catch::Approx(channel(want, IM_COL32_G_SHIFT)).margin(1.0));
        CHECK(channel(v.col, IM_COL32_B_SHIFT)
            == Catch::Approx(channel(want, IM_COL32_B_SHIFT)).margin(1.0));
        ++plate_vertices;
        lowest_x = std::min(lowest_x, v.pos.x);
        highest_x = std::max(highest_x, v.pos.x);
    }
    CHECK(plate_vertices > 20);
    CHECK(lowest_x < left + 1.0f);
    CHECK(highest_x > right - 1.0f);
}

TEST_CASE("the bar is translucent: the desk behind shows through its plate",
    "[menu_bar][look]") {
    CHECK(theme::menu_bar_alpha < 1.0f);
    CHECK(theme::menu_bar_alpha > 0.5f);
    for (const auto& stop : theme::menu_bar_stops)
        CHECK(((stop.colour >> IM_COL32_A_SHIFT) & 0xffu) < 0xffu);
    CHECK(theme::menu_bar_edge().w < theme::menu_border().w);

    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    /* No opaque fill of the window's own sits under the plate. */
    CHECK((bar->Flags & ImGuiWindowFlags_NoBackground) != 0);

    software_raster raster(1280, 800);
    raster.update_textures();
    const ImU32 wall = IM_COL32(200, 200, 220, 255);
    raster.draw(*ImGui::GetDrawData(), wall);
    const int y = static_cast<int>(bar->Pos.y + bar->Size.y * 0.5f);
    const float a = theme::menu_bar_alpha;
    const auto expect = [&](float x, ImU32 stop) {
        const ImVec4 got = raster.pixel(static_cast<int>(x), y);
        const auto blended = [&](unsigned int shift, float wall_c) {
            return (static_cast<float>((stop >> shift) & 0xffu) / 255.0f) * a
                + wall_c * (1.0f - a);
        };
        INFO("x " << x << " got " << got.x << " " << got.y << " " << got.z);
        CHECK(got.x == Catch::Approx(blended(IM_COL32_R_SHIFT, 200.0f / 255.0f))
            .margin(0.02));
        CHECK(got.z == Catch::Approx(blended(IM_COL32_B_SHIFT, 220.0f / 255.0f))
            .margin(0.02));
    };
    const float left = bar->Pos.x + theme::menu_bar_margin;
    const float width = bar->Size.x - 2.0f * theme::menu_bar_margin;
    const std::span<const theme::gradient_stop> run(theme::menu_bar_stops);
    /* Away from the logo and the labels, where only the plate is drawn. */
    for (const float t : {0.60f, 0.75f, 0.9f, 0.97f})
        expect(left + t * width, theme::stops_colour(run, t));
    /* Across the knee there is no seam, dark or light. */
    for (const float t : {0.24f, 0.25f, 0.26f})
        expect(left + t * width, theme::stops_colour(run, t));
}

TEST_CASE("the plate is rounded and inset: its corner and its margin show the desk",
    "[menu_bar][look]") {
    CHECK(theme::menu_bar_rounding >= 8.0f);
    CHECK(theme::menu_bar_margin > 0.0f);
    bar_harness h;
    h.settle();
    const ImGuiWindow* bar = h.bar();
    REQUIRE(bar != nullptr);
    software_raster raster(1280, 800);
    raster.update_textures();
    const ImU32 wall = IM_COL32(200, 200, 220, 255);
    raster.draw(*ImGui::GetDrawData(), wall);
    const auto is_wall = [&](int x, int y) {
        const ImVec4 p = raster.pixel(x, y);
        return std::abs(p.x - 200.0f / 255.0f) < 0.01f
            && std::abs(p.z - 220.0f / 255.0f) < 0.01f;
    };
    const int left = static_cast<int>(bar->Pos.x + theme::menu_bar_margin);
    const int top = static_cast<int>(bar->Pos.y + theme::menu_bar_margin);
    const int mid = top + static_cast<int>(theme::menu_bar_height * 0.5f);
    const int right = static_cast<int>(bar->Pos.x + bar->Size.x
        - theme::menu_bar_margin) - 1;
    /* The very corner pixel is outside the arc, an edge pixel mid-height is in. */
    CHECK(is_wall(left, top));
    CHECK(is_wall(right, top));
    CHECK_FALSE(is_wall(left + 1, mid));
    CHECK_FALSE(is_wall(right - 1, mid));
    /* The margin above, left and below the plate is the desk. */
    CHECK(is_wall(left + 40, top - 2));
    CHECK(is_wall(left - 2, mid));
    CHECK(is_wall(left + 40, top + static_cast<int>(theme::menu_bar_height) + 2));
    CHECK_FALSE(is_wall(left + 400, mid));
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

TEST_CASE("a menu opens a small gap below the bar's plate", "[menu_bar]") {
    for (const char* name : {"File", "Tools"}) {
        bar_harness h;
        h.open_menu(name);
        REQUIRE(GImGui->OpenPopupStack.Size > 0);
        const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
        const ImGuiWindow* bar = bar_harness::bar();
        const float plate_bottom = bar->Pos.y + theme::menu_bar_margin
            + theme::menu_bar_height;
        CHECK(theme::menu_popup_gap > 0.0f);
        CHECK(popup->Pos.y - plate_bottom
            == Catch::Approx(theme::menu_popup_gap));
    }
}

TEST_CASE("an open top-level menu's caret points at its label's centre",
    "[menu_bar][glass]") {
    for (const char* name : {"File", "Tools"}) {
        bar_harness h;
        h.open_menu(name);
        REQUIRE(GImGui->OpenPopupStack.Size > 0);
        const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
        const auto label = h.label(name);
        REQUIRE(label.has_value());
        const auto& front = ImGui::GetForegroundDrawList(
            ImGui::GetMainViewport())->VtxBuffer;
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(theme::menu_surface());
        float apex_x = -1.0f;
        float apex_y = 1.0e9f;
        for (const ImDrawVert& v : front) {
            if (v.col == fill && v.pos.y < apex_y) {
                apex_y = v.pos.y;
                apex_x = v.pos.x;
            }
        }
        const float plate_bottom = popup->Pos.y - theme::menu_popup_gap;
        /* The label centre was found by a 2 pixel sweep, so allow that much. */
        CHECK(apex_x == Catch::Approx(label->x).margin(2.0f));
        CHECK(apex_y == Catch::Approx(plate_bottom + 1.0f).margin(1.5f));
    }
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

namespace {

void add_instance(bar_harness& h, const char* id, const char* name) {
    model::domain::instance row;
    row.id = id;
    row.name = name;
    static_cast<void>(h.desk.instances.upsert(row));
}

void pin_accounts(bar_harness& h, const char* instance_id) {
    model::domain::shortcut row;
    row.id = std::string("accounts-") + instance_id;
    row.target_kind = model::domain::window_kind::accounts;
    row.target_id = instance_id;
    h.desk.shortcuts.upsert(row);
}

/** @brief The signals raised since the last call. */
std::vector<model::signal::signal> drained_signals() {
    return model::signal::bus().drain();
}

}

TEST_CASE("TOOLS: Accounts is a submenu row with the session's instance first",
    "[menu_bar][tools]") {
    bar_harness h;
    add_instance(h, "i", "inst");
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    h.settle();
    /* The child panel is the top of the popup stack, beside the TOOLS panel. */
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    h.click(bar_harness::row_centre(0));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::accounts);
    CHECK(commands[0].instance_id == "i");
}

TEST_CASE("TOOLS > Accounts lists one row per accounts shortcut, once each",
    "[menu_bar][tools]") {
    bar_harness h;
    add_instance(h, "i", "inst");
    add_instance(h, "j", "other");
    add_instance(h, "k", "third");
    pin_accounts(h, "j");
    pin_accounts(h, "i");
    pin_accounts(h, "k");
    /* A second shortcut to the same instance adds no second row. */
    model::domain::shortcut twin;
    twin.id = "accounts-j-again";
    twin.target_kind = model::domain::window_kind::accounts;
    twin.target_id = "j";
    h.desk.shortcuts.upsert(twin);
    model::domain::shortcut surface;
    surface.id = "instances";
    surface.target_kind = model::domain::window_kind::instance_manager;
    h.desk.shortcuts.upsert(surface);
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    h.settle();
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    const float row = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    /* i (the session's), j, k: three rows tall and no more. */
    CHECK(panel->Size.y == Catch::Approx(2.0f * theme::menu_padding
        + 3.0f * row + 2.0f * theme::menu_row_gap).margin(1.0f));
    /* Beside the Accounts row, not under it. */
    const ImGuiWindow* tools = GImGui->OpenPopupStack[0].Window;
    CHECK(panel->Pos.x >= tools->Pos.x + tools->Size.x
        - theme::menu_padding - 0.5f);

    h.click(bar_harness::row_centre(2));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::accounts);
    CHECK(commands[0].instance_id == "k");
}

TEST_CASE("TOOLS > Accounts: a pick raises the activation signal and closes the "
          "menu",
    "[menu_bar][tools]") {
    bar_harness h;
    add_instance(h, "i", "inst");
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    h.settle();
    static_cast<void>(drained_signals());
    h.click(bar_harness::row_centre(0));
    const auto raised = drained_signals();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].name == model::signal::names::shortcut_activated);
    CHECK(raised[0].detail == "Open the accounts of inst");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("TOOLS > Accounts is disabled with no session and no shortcut",
    "[menu_bar][tools]") {
    bar_harness h;
    h.desk.sessions.set_status(h.desk.session,
        model::domain::session_status::disconnected);
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    h.settle();
    CHECK(GImGui->OpenPopupStack.Size == 1);
    CHECK(h.commands().empty());
}

TEST_CASE("TOOLS > Accounts still lists the shortcuts of a signed-out store",
    "[menu_bar][tools]") {
    bar_harness h;
    h.desk.sessions.set_status(h.desk.session,
        model::domain::session_status::disconnected);
    add_instance(h, "j", "other");
    pin_accounts(h, "j");
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    h.settle();
    h.click(bar_harness::row_centre(0));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].instance_id == "j");
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
    CHECK(bar_bottom == Catch::Approx(theme::menu_bar_height
        + 2.0f * theme::menu_bar_margin));
    CHECK(viewport->WorkPos.y == Catch::Approx(bar_bottom));
    CHECK(space->Pos.y == Catch::Approx(bar_bottom));
    CHECK(viewport->WorkSize.y
        == Catch::Approx(viewport->Size.y - theme::menu_bar_extent));
    /* Nothing is inset at the foot: the desk ends at the viewport's edge. */
    CHECK(viewport->WorkSize.y + viewport->WorkPos.y
        == Catch::Approx(viewport->Size.y));
    CHECK(space->Pos.y + space->Size.y == Catch::Approx(viewport->Size.y));
}

TEST_CASE("pinned shortcuts draw nothing at the foot and inset nothing",
    "[menu_bar][workspace]") {
    headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    model::domain::shortcut row;
    row.id = "s";
    row.target_kind = model::domain::window_kind::accounts;
    row.target_id = "i";
    desk.shortcuts.upsert(row);
    render::workspace workspace;
    auto input = desk.input();
    for (int i = 0; i < 4; ++i) {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindow* space = ImGui::FindWindowByName("###hedgr-desk");
    REQUIRE(space != nullptr);
    CHECK(space->Pos.y + space->Size.y == Catch::Approx(viewport->Size.y));
    CHECK(ImGui::FindWindowByName("##hedgr-shortcuts") == nullptr);
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
    CHECK(bar->Size.y == Catch::Approx(theme::menu_bar_extent));
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
        theme::menu_bar_extent * 0.5f);
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

TEST_CASE("the bundled monospace is found beside the executable or in the source "
          "tree, and nowhere else",
    "[menu_bar][fonts]") {
    namespace icon = hedgr::ui::icon;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hedgr-mono-font-test";
    fs::remove_all(root);
    fs::create_directories(root / "bin");
    CHECK(icon::mono_font_path(root / "bin", root).empty());

    fs::create_directories(root / "assets" / "fonts");
    std::ofstream(root / "assets" / "fonts" / "JetBrainsMono-Regular.ttf") << "x";
    CHECK(icon::mono_font_path(root / "bin", root)
        == root / "assets" / "fonts" / "JetBrainsMono-Regular.ttf");

    fs::create_directories(root / "share" / "hedgr.ui" / "fonts");
    std::ofstream(root / "share" / "hedgr.ui" / "fonts"
        / "JetBrainsMono-Regular.ttf") << "x";
    CHECK(icon::mono_font_path(root / "bin", root)
        == root / "share" / "hedgr.ui" / "fonts" / "JetBrainsMono-Regular.ttf");
    fs::remove_all(root);
}

TEST_CASE("the monospace is the base face until the fonts are loaded",
    "[menu_bar][fonts]") {
    headless_imgui imgui;
    ImGui::GetIO().Fonts->AddFontDefaultVector();
    CHECK(hedgr::ui::icon::mono_font() == ImGui::GetIO().Fonts->Fonts[0]);
}

TEST_CASE("the shipped monospace loads, is its own face, and sets the menu "
          "labels",
    "[menu_bar][fonts]") {
    headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();
    ImFont* const mono = hedgr::ui::icon::mono_font();
    ImFont* const base = ImGui::GetIO().Fonts->Fonts[0];
    REQUIRE(mono != nullptr);
    REQUIRE(mono != base);
    CHECK(hedgr::ui::icon::numeric_font() == mono);

    /* A monospace gives every glyph one advance; the base face does not. */
    const auto advance = [](ImFont* font, char c) {
        const char text[2] = {c, '\0'};
        return font->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, text).x;
    };
    CHECK(advance(mono, 'I') == Catch::Approx(advance(mono, 'W')));
    CHECK(advance(base, 'I') != Catch::Approx(advance(base, 'W')));

    float width = 0.0f;
    hedgr::testing::frame([&] {
        static_cast<void>(widget::menu_label("MMMM", false, 30.0f));
        width = ImGui::GetItemRectSize().x;
    });
    const float spacing = 15.0f * theme::menu_label_spacing_share;
    const float expected = 4.0f * (advance(mono, 'M') + spacing) - spacing
        + 2.0f * theme::menu_label_padding_x;
    const float in_base = 4.0f * (advance(base, 'M') + spacing) - spacing
        + 2.0f * theme::menu_label_padding_x;
    CHECK(width == Catch::Approx(expected));
    CHECK(width != Catch::Approx(in_base));
}

TEST_CASE("the desk's dock tabs are set in the monospace, the windows' bodies "
          "are not",
    "[menu_bar][fonts][docking]") {
    headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"},
        "left"));
    render::workspace workspace;
    auto input = desk.input();
    ImGuiID space = 0;
    for (int i = 0; i < 5; ++i) {
        ImGui::NewFrame();
        if (i == 1) {
            const ImGuiWindow* host = ImGui::FindWindowByName("###hedgr-desk");
            REQUIRE(host != nullptr);
            space = ImHashStr("desk", 0, host->ID);
            ImGui::DockBuilderDockWindow("###left", space);
        }
        workspace.render(input);
        ImGui::Render();
    }
    const ImGuiWindow* chart = ImGui::FindWindowByName("###left");
    REQUIRE(chart != nullptr);
    REQUIRE(chart->DockNode != nullptr);
    REQUIRE(chart->DockNode->TabBar != nullptr);
    const ImGuiTabItem* tab =
        ImGui::TabBarFindTabByID(chart->DockNode->TabBar, chart->TabId);
    REQUIRE(tab != nullptr);

    const auto width_in = [&](ImFont* font) {
        ImGui::NewFrame();
        ImGui::PushFont(font);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(theme::tab_padding_x, theme::tab_padding_y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
            ImVec2(theme::tab_spacing, 0.0f));
        const float w = ImGui::TabItemCalcSize(chart->Name, true).x;
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::Render();
        return w;
    };
    const float mono = width_in(hedgr::ui::icon::mono_font());
    const float base = width_in(ImGui::GetIO().Fonts->Fonts[0]);
    REQUIRE(mono != Catch::Approx(base));
    CHECK(tab->ContentWidth == Catch::Approx(mono).margin(0.5));
}

TEST_CASE("the desk's dock tabs are 28 pixels tall and four apart, and the "
          "style is put back",
    "[menu_bar][fonts][docking]") {
    headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    theme::apply_theme();
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"},
        "left"));
    static_cast<void>(desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX EUR/USD", .series_id = "t"},
        "right"));
    render::workspace workspace;
    auto input = desk.input();
    const ImGuiStyle before = ImGui::GetStyle();
    for (int i = 0; i < 5; ++i) {
        ImGui::NewFrame();
        if (i == 1) {
            const ImGuiWindow* host = ImGui::FindWindowByName("###hedgr-desk");
            REQUIRE(host != nullptr);
            const ImGuiID space = ImHashStr("desk", 0, host->ID);
            ImGui::DockBuilderDockWindow("###left", space);
            ImGui::DockBuilderDockWindow("###right", space);
        }
        workspace.render(input);
        ImGui::Render();
    }
    const ImGuiWindow* left = ImGui::FindWindowByName("###left");
    REQUIRE(left != nullptr);
    REQUIRE(left->DockNode != nullptr);
    const ImGuiTabBar* bar = left->DockNode->TabBar;
    REQUIRE(bar != nullptr);
    REQUIRE(bar->Tabs.Size == 2);
    /* The reference's tab is 28 pixels from top to the rule. */
    CHECK(bar->BarRect.GetHeight() == Catch::Approx(28.0f));
    const ImGuiTabItem& first = bar->Tabs[0];
    const ImGuiTabItem& second = bar->Tabs[1];
    const ImGuiTabItem& lead = first.Offset < second.Offset ? first : second;
    const ImGuiTabItem& next = first.Offset < second.Offset ? second : first;
    CHECK(next.Offset - (lead.Offset + lead.Width) == Catch::Approx(4.0f));

    const ImGuiStyle& after = ImGui::GetStyle();
    CHECK(after.FramePadding.y == before.FramePadding.y);
    CHECK(after.ItemInnerSpacing.x == before.ItemInnerSpacing.x);
    CHECK(after.Colors[ImGuiCol_Text].x == before.Colors[ImGuiCol_Text].x);
    CHECK(after.Colors[ImGuiCol_TitleBg].x == before.Colors[ImGuiCol_TitleBg].x);
}

TEST_CASE("every window kind's icon is a glyph of the monospace the dock tabs "
          "use",
    "[menu_bar][fonts]") {
    headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    ImFont* const mono = hedgr::ui::icon::mono_font();
    ImFont* const base = ImGui::GetIO().Fonts->Fonts[0];
    REQUIRE(mono != base);
    ImFontBaked* const baked = mono->GetFontBaked(15.0f);
    const char* const glyph = theme::window_kind_icon(
        model::domain::window_kind::chart);
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, glyph, nullptr);
    REQUIRE(codepoint != 0);
    CHECK(baked->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr);
}

TEST_CASE("a menu popup's rows are set in the monospace and the font is put back",
    "[menu_bar][fonts]") {
    headless_imgui imgui;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();
    ImFont* const base = ImGui::GetIO().Fonts->Fonts[0];
    ImFont* inside = nullptr;
    ImFont* after = nullptr;
    for (int i = 0; i < 3; ++i) {
        hedgr::testing::frame([&] {
            if (i == 0)
                ImGui::OpenPopup("##mono-popup");
            {
                widget::menu_popup popup("##mono-popup");
                if (popup)
                    inside = ImGui::GetFont();
            }
            after = ImGui::GetFont();
        });
    }
    REQUIRE(inside != nullptr);
    CHECK(inside == hedgr::ui::icon::mono_font());
    CHECK(inside != base);
    CHECK(after == base);
}
