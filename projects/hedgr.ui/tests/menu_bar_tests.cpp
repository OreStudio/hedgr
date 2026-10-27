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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/status_bar.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
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
#include <cstddef>
#include <string>
#include <string_view>
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
            for (float x = 2.0f; x < 800.0f; x += 2.0f) {
                hover(ImVec2(x, theme::menu_bar_extent * 0.5f));
                for (const char* n :
                    {"File", "View", "Instances", "Accounts", "Tools", "Help"}) {
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
    const auto expect = [&](float x, ImU32 stop, int row) {
        const ImVec4 got = raster.pixel(static_cast<int>(x), row);
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
        expect(left + t * width, theme::stops_colour(run, t), y);
    /*
     * Across the knee there is no seam, dark or light. The labels reach the
     * knee now, so it is read on a row just under the plate's top edge, above
     * their text.
     */
    const int top_row = static_cast<int>(bar->Pos.y + theme::menu_bar_margin)
        + 3;
    for (const float t : {0.24f, 0.25f, 0.26f})
        expect(left + t * width, theme::stops_colour(run, t), top_row);
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

TEST_CASE("the bar leads with File, View, Instances, Accounts, Tools and Help in "
          "that order",
    "[menu_bar]") {
    bar_harness h;
    const auto file = h.label("File");
    const auto view = h.label("View");
    const auto instances = h.label("Instances");
    const auto accounts = h.label("Accounts");
    const auto tools = h.label("Tools");
    const auto help = h.label("Help");
    REQUIRE((file && view && instances && accounts && tools && help));
    CHECK(file->x > theme::menu_bar_height);
    CHECK(file->x < view->x);
    CHECK(view->x < instances->x);
    CHECK(instances->x < accounts->x);
    CHECK(accounts->x < tools->x);
    CHECK(tools->x < help->x);
}

TEST_CASE("hovering across the real labels moves the open menu", "[menu_bar]") {
    bar_harness h;
    h.open_menu("File");
    REQUIRE(bar_harness::popup_open("File"));
    h.hover(*h.label("Tools"));
    h.settle();
    CHECK(h.state.open == 4);
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
    CHECK_FALSE(h.label("Instances").has_value());
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

TEST_CASE("VIEW: Toolbar flips whether the floating toolbar is shown",
    "[menu_bar][view]") {
    bar_harness h;
    REQUIRE(h.desk.market_view.toolbar_visible);
    h.open_menu("View");
    h.click(bar_harness::row_centre(2));
    CHECK_FALSE(h.desk.market_view.toolbar_visible);
    h.open_menu("View");
    h.click(bar_harness::row_centre(2));
    CHECK(h.desk.market_view.toolbar_visible);
}

TEST_CASE("VIEW: Reference providers flips whether they are listed",
    "[menu_bar][view]") {
    bar_harness h;
    REQUIRE_FALSE(h.desk.market_view.show_reference_providers);
    h.open_menu("View");
    h.click(bar_harness::row_centre(3));
    CHECK(h.desk.market_view.show_reference_providers);
    h.open_menu("View");
    h.click(bar_harness::row_centre(3));
    CHECK_FALSE(h.desk.market_view.show_reference_providers);
}

TEST_CASE("VIEW: Full screen asks the application to switch full screen",
    "[menu_bar][view]") {
    bar_harness h;
    h.open_menu("View");
    REQUIRE(bar_harness::popup_open("View"));
    h.click(bar_harness::row_centre(1));
    CHECK(h.result.fullscreen_toggled);
    CHECK_FALSE(h.result.undock_all);
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

/** @brief The signals raised since the last call. */
std::vector<model::signal::signal> drained_signals() {
    return model::signal::bus().drain();
}

}

TEST_CASE("TOOLS: Command centre is there for an administrator's session only",
    "[menu_bar][tools]") {
    bar_harness h;
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
    CHECK(h.commands().empty());

    h.desk.sessions.set_mode(h.desk.session,
        model::domain::session_mode::system_administration);
    h.open_menu("Tools");
    h.click(bar_harness::row_centre(1));
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

TEST_CASE("the work area is inset by the top and bottom bars exactly once, and "
          "the desk fills what is left",
    "[menu_bar][workspace][status_bar]") {
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
    const ImGuiWindow* strip = ImGui::FindWindowByName("###hedgr-status-bar");
    const ImGuiWindow* space = ImGui::FindWindowByName("###hedgr-desk");
    REQUIRE(bar != nullptr);
    REQUIRE(strip != nullptr);
    REQUIRE(space != nullptr);
    const float bar_bottom = bar->Pos.y + bar->Size.y;
    const float strip_top = strip->Pos.y;
    CHECK(bar_bottom == Catch::Approx(theme::menu_bar_height
        + 2.0f * theme::menu_bar_margin));
    CHECK(strip_top == Catch::Approx(viewport->Size.y
        - theme::status_bar_height));
    CHECK(viewport->WorkPos.y == Catch::Approx(bar_bottom));
    CHECK(space->Pos.y == Catch::Approx(bar_bottom));
    CHECK(viewport->WorkSize.y == Catch::Approx(viewport->Size.y
        - theme::menu_bar_extent - theme::status_bar_height));
    CHECK(space->Pos.y + space->Size.y == Catch::Approx(strip_top));
}

TEST_CASE("pinned shortcuts add no foot of their own and no second inset",
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
    const ImGuiWindow* strip = ImGui::FindWindowByName("###hedgr-status-bar");
    const ImGuiWindow* space = ImGui::FindWindowByName("###hedgr-desk");
    REQUIRE(strip != nullptr);
    REQUIRE(space != nullptr);
    CHECK(space->Pos.y + space->Size.y == Catch::Approx(strip->Pos.y));
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

namespace {

/** @brief The scrolling grid's window, or null while the panel is closed. */
const ImGuiWindow* grid_window() {
    for (const ImGuiWindow* window : GImGui->Windows) {
        if (std::string_view(window->Name).find("instances-grid")
            != std::string_view::npos)
            return window;
    }
    return nullptr;
}

/** @brief The centre of the n-th tile of the grid's first row. */
ImVec2 tile_centre(int n) {
    const ImGuiWindow* grid = grid_window();
    REQUIRE(grid != nullptr);
    const float width = theme::instances_tile_photo
        + 2.0f * theme::instances_tile_pad;
    return ImVec2(grid->InnerRect.Min.x
            + static_cast<float>(n) * (width + theme::instances_tile_gap)
            + width * 0.5f,
        grid->InnerRect.Min.y + theme::instances_tile_pad
            + theme::instances_tile_photo * 0.5f);
}

/** @brief The centre of the first pinned tile, at the panel's top left. */
ImVec2 first_pin_centre() {
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    return ImVec2(panel->Pos.x + theme::menu_padding + 24.0f,
        panel->Pos.y + theme::menu_padding + theme::instances_tile_pad
            + theme::instances_pin_photo * 0.5f);
}

void pin(bar_harness& h, model::domain::window_kind kind, const char* target) {
    model::domain::shortcut row;
    row.id = std::string("pin-") + std::string(model::domain::to_string(kind))
        + target;
    row.target_kind = kind;
    row.target_id = target;
    h.desk.shortcuts.upsert(row);
}

/** @brief How many vertices of @p colour the frame drew, in every window. */
std::size_t vertices_of(ImU32 colour) {
    std::size_t count = 0;
    for (const ImGuiWindow* window : GImGui->Windows) {
        for (const ImDrawVert& v : window->DrawList->VtxBuffer)
            count += v.col == colour ? 1 : 0;
    }
    return count;
}

std::size_t open_commands(bar_harness& h, model::domain::window_kind kind) {
    std::size_t count = 0;
    for (const auto& c : h.commands()) {
        if (c.kind == model::command::command_kind::open_window
            && c.new_window_kind == kind)
            ++count;
    }
    return count;
}

void two_instances(bar_harness& h) {
    add_instance(h, "a-1", "Alpha");
    add_instance(h, "b-2", "Beta");
}

}

TEST_CASE("the Instances panel opens on a click and the pointer on a label "
          "switches menus both ways",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    h.open_menu("Instances");
    CHECK(bar_harness::popup_open("Instances"));
    CHECK(h.state.open == 2);
    h.hover(*h.label("Tools"));
    h.settle();
    CHECK(bar_harness::popup_open("Tools"));
    CHECK_FALSE(bar_harness::popup_open("Instances"));
    h.hover(*h.label("Instances"));
    h.settle();
    CHECK(bar_harness::popup_open("Instances"));
    CHECK_FALSE(bar_harness::popup_open("Tools"));
}

TEST_CASE("the Instances panel is the themed large size, and a small window "
          "caps it to the work area",
    "[menu_bar][instances_panel]") {
    {
        bar_harness h;
        h.open_menu("Instances");
        const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
        CHECK(panel->Size.x == Catch::Approx(theme::instances_panel_width));
        CHECK(panel->Size.y == Catch::Approx(theme::instances_panel_height));
    }
    {
        bar_harness h;
        ImGui::GetIO().DisplaySize = ImVec2(500.0f, 400.0f);
        h.open_menu("Instances");
        const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
        CHECK(panel->Size.x < theme::instances_panel_width);
        CHECK(panel->Size.y < theme::instances_panel_height);
        CHECK(panel->Pos.x >= 0.0f);
        CHECK(panel->Pos.y >= 0.0f);
        CHECK(panel->Pos.x + panel->Size.x <= 500.0f + 0.5f);
        CHECK(panel->Pos.y + panel->Size.y <= 400.0f + 0.5f);
    }
}

TEST_CASE("typing in the panel's filter narrows the tiles by name or id, in any case",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    add_instance(h, "a-1", "Alpha");
    add_instance(h, "b-2", "Beta");
    add_instance(h, "alp-3", "Gamma");
    h.open_menu("Instances");
    CHECK(h.state.instances.matches.size() == 3);
    CHECK(h.state.instances.total == 3);

    ImGui::GetIO().AddInputCharactersUTF8("ALP");
    h.settle();
    CHECK(std::string(h.state.instances.filter.text) == "ALP");
    /* Alpha by name, Gamma by its id alp-3. */
    CHECK(h.state.instances.matches.size() == 2);
    CHECK(h.state.instances.total == 3);

    ImGui::GetIO().AddInputCharactersUTF8("-3");
    h.settle();
    CHECK(h.state.instances.matches.size() == 1);
}

TEST_CASE("the panel opens with an empty, focused filter each time",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    h.open_menu("Instances");
    ImGui::GetIO().AddInputCharactersUTF8("bet");
    h.settle();
    REQUIRE(h.state.instances.matches.size() == 1);
    CHECK(GImGui->ActiveId != 0);

    h.state.open = menu_bar_state::none;
    h.settle();
    h.open_menu("Instances");
    CHECK(std::string(h.state.instances.filter.text).empty());
    CHECK(h.state.instances.matches.size() == 2);
    CHECK(GImGui->ActiveId != 0);
}

TEST_CASE("the filtered list is rebuilt when the table changes and not otherwise",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    h.open_menu("Instances");
    REQUIRE(h.state.instances.matches.size() == 2);

    /* Nothing changed: a planted repeat survives, so nothing was rebuilt. */
    h.state.instances.matches.push_back(1);
    h.settle();
    CHECK(h.state.instances.matches.size() == 3);
    CHECK(h.state.instances.matches[2] == 1);

    add_instance(h, "c-3", "Charlie");
    h.settle();
    REQUIRE(h.state.instances.matches.size() == 3);
    CHECK(h.state.instances.matches[2] == 2);
    CHECK(h.state.instances.total == 3);
}

TEST_CASE("a tile click raises the Instances window, says so, and closes the panel",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    h.open_menu("Instances");
    static_cast<void>(h.commands());
    static_cast<void>(drained_signals());

    h.click(tile_centre(1));
    CHECK(h.result.focus_instances);
    CHECK(open_commands(h, model::domain::window_kind::instance_manager) == 1);
    const auto raised = drained_signals();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].name == model::signal::names::shortcut_activated);
    CHECK(raised[0].detail == "Open the Instances window on Beta");
    CHECK(GImGui->OpenPopupStack.Size == 0);

    /* The raised window shows the picked instance's pane. */
    auto input = h.desk.input();
    render::window_ui_state ui;
    widget::status_bar status;
    render::lock_control lock;
    const auto window = h.desk.open(model::domain::window_kind::instance_manager,
        model::domain::notice_state{}, "instance-manager");
    hedgr::testing::frame([&] {
        render::render_instances_window(window, input, ui, status, lock);
    });
    CHECK(ui.browser.selected_leaf == "b-2");
}

TEST_CASE("tiles are drawn in name order, not the table's", "[menu_bar][instances_panel]") {
    bar_harness h;
    add_instance(h, "z-1", "Zeta");
    add_instance(h, "a-1", "Alpha");
    h.open_menu("Instances");
    const auto alpha = h.desk.instances.find("a-1");
    const auto zeta = h.desk.instances.find("z-1");
    REQUIRE((alpha && zeta));
    REQUIRE(h.state.instances.matches.size() == 2);
    CHECK(h.state.instances.matches[0] == alpha->index);
    CHECK(h.state.instances.matches[1] == zeta->index);
}

TEST_CASE("a Ctrl-click on tiles picks each and leaves the panel open",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    h.open_menu("Instances");
    static_cast<void>(h.commands());

    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    h.click(tile_centre(0));
    h.click(tile_centre(1));
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    h.frame();
    CHECK(open_commands(h, model::domain::window_kind::instance_manager) == 2);
    CHECK(GImGui->OpenPopupStack.Size == 1);
    CHECK(bar_harness::popup_open("Instances"));
}

TEST_CASE("a pinned instances shortcut raises the window like the old bar",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    pin(h, model::domain::window_kind::instance_manager, "");
    h.open_menu("Instances");
    static_cast<void>(h.commands());
    static_cast<void>(drained_signals());

    h.click(first_pin_centre());
    CHECK(h.result.focus_instances);
    CHECK(open_commands(h, model::domain::window_kind::instance_manager) == 1);
    const auto raised = drained_signals();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].name == model::signal::names::shortcut_activated);
    CHECK(raised[0].detail == "Bring the Instances window to the front");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("a pinned accounts shortcut opens that instance's accounts",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    pin(h, model::domain::window_kind::accounts, "b-2");
    h.open_menu("Instances");
    static_cast<void>(h.commands());
    static_cast<void>(drained_signals());

    h.click(first_pin_centre());
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].new_window_kind == model::domain::window_kind::accounts);
    CHECK(commands[0].instance_id == "b-2");
    CHECK_FALSE(h.result.focus_instances);
    const auto raised = drained_signals();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].detail == "Open the accounts of Beta");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("a pinned shortcut of a kind that cannot open does nothing and stays",
    "[menu_bar][instances_panel]") {
    bar_harness h;
    pin(h, model::domain::window_kind::chart, "x");
    h.open_menu("Instances");
    static_cast<void>(h.commands());
    static_cast<void>(drained_signals());

    h.click(first_pin_centre());
    CHECK(h.commands().empty());
    CHECK(drained_signals().empty());
    CHECK(GImGui->OpenPopupStack.Size == 1);
}

TEST_CASE("a tile carries the probe's colour as a status dot", "[menu_bar][instances_panel]") {
    bar_harness h;
    two_instances(h);
    h.open_menu("Instances");
    CHECK(vertices_of(theme::failure_colour) == 0);

    model::domain::probe_result down;
    down.reach = model::domain::reachability::unreachable;
    h.desk.probes.set(*h.desk.instances.find("a-1"), down);
    h.settle();
    CHECK(vertices_of(theme::failure_colour) > 0);
}

TEST_CASE("no instances and no filter match both draw the no data mark and no tile",
    "[menu_bar][instances_panel]") {
    const ImU32 quiet = theme::view_quiet_colour;
    {
        bar_harness h;
        two_instances(h);
        h.open_menu("Instances");
        CHECK(vertices_of(quiet) == 0);
    }
    {
        bar_harness h;
        h.open_menu("Instances");
        CHECK(h.state.instances.matches.empty());
        CHECK(vertices_of(quiet) > 0);
    }
    {
        bar_harness h;
        two_instances(h);
        h.open_menu("Instances");
        ImGui::GetIO().AddInputCharactersUTF8("zzz");
        h.settle();
        CHECK(h.state.instances.matches.empty());
        CHECK(h.state.instances.total == 2);
        CHECK(vertices_of(quiet) > 0);
    }
}

namespace {

void add_account(bar_harness& h, const char* id, const char* instance_id,
    const char* name, const char* principal, std::string_view folder_id = {},
    bool has_credential = false) {
    model::domain::account row;
    row.id = id;
    row.instance_id = instance_id;
    row.name = name;
    row.principal = principal;
    row.folder_id = std::string{folder_id};
    row.has_credential = has_credential;
    static_cast<void>(h.desk.accounts.upsert(row));
}

/** @brief Adds one folder to an instance's accounts tree and returns its id. */
std::string add_accounts_folder(bar_harness& h, std::string id,
    std::string instance_id, std::string parent_id, std::string name) {
    model::domain::folder row;
    row.id = std::move(id);
    row.parent_id = std::move(parent_id);
    row.owner_id = std::move(instance_id);
    row.name = std::move(name);
    row.tree = model::domain::folder_tree::accounts;
    REQUIRE(h.desk.folders.upsert(row).has_value());
    return row.id;
}

/** @brief Gives one instance an accounts tree with a Privileged folder. */
std::string privileged_folder(bar_harness& h, const char* instance_id) {
    const std::string root = add_accounts_folder(h,
        model::domain::accounts_root_id_for(instance_id), instance_id, "",
        "Accounts");
    return add_accounts_folder(h, std::string("privileged-") + instance_id,
        instance_id, root, "Privileged");
}

}

namespace {

/** @brief The child window the open popup made, which holds its rows. */
const ImGuiWindow* popup_child() {
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    for (ImGuiWindow* window : GImGui->Windows)
        if (window->ParentWindow == panel)
            return window;
    return nullptr;
}

/** @brief Sweeps @p x down the open popup for @p target, and returns its y. */
std::optional<float> find_popup_row(bar_harness& h, float x, ImGuiID target) {
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    for (float y = panel->Pos.y; y < panel->Pos.y + panel->Size.y; y += 2.0f) {
        h.hover(ImVec2(x, y));
        if (GImGui->HoveredId == target)
            return y;
    }
    return std::nullopt;
}

void open_windows_list(bar_harness& h) {
    h.open_menu("View");
    h.click(bar_harness::row_centre(4));
    h.settle();
}

/** @brief Opens one environment's submenu from the ACCOUNTS menu. */
void open_environment(bar_harness& h, int row = 0) {
    h.open_menu("Accounts");
    h.click(bar_harness::row_centre(row));
    h.settle();
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
}

/** @brief The y of a menu row whose label seeds @p id in the open popup. */
std::optional<float> find_account_row(bar_harness& h, const char* id) {
    const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
    const ImGuiID target =
        ImHashStr("##row", 0, ImHashStr(id, 0, popup->ID));
    return find_popup_row(h, popup->Pos.x + 30.0f, target);
}

}

TEST_CASE("ACCOUNTS lists the environments that have accounts, by name",
    "[menu_bar][accounts]") {
    bar_harness h;
    add_instance(h, "z", "Zeta");
    add_instance(h, "a", "Alpha");
    add_instance(h, "e", "Empty");
    add_account(h, "z-1", "z", "Zed", "zed@example", {}, true);
    add_account(h, "a-1", "a", "Abe", "abe@example", {}, true);

    h.open_menu("Accounts");
    const ImGuiWindow* menu = GImGui->OpenPopupStack.back().Window;
    const float row = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    /* Alpha and Zeta, and no row for the environment with no login. */
    CHECK(menu->Size.y == Catch::Approx(2.0f * theme::menu_padding
        + 2.0f * row + theme::menu_row_gap).margin(1.0f));

    h.click(bar_harness::row_centre(0));
    h.settle();
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    const auto y = find_account_row(h, "Abe");
    REQUIRE(y.has_value());
    static_cast<void>(h.commands());
    h.click(ImVec2(GImGui->OpenPopupStack.back().Window->Pos.x + 30.0f, *y));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::connect_session);
    CHECK(commands[0].instance_id == "a");
}

TEST_CASE("an ACCOUNTS account signs in at once with the memorised password",
    "[menu_bar][accounts]") {
    bar_harness h;
    add_instance(h, "i", "Brave Hopper");
    const std::string folder = privileged_folder(h, "i");
    add_account(h, "a-super", "i", "super_admin", "super@acme", folder, true);
    add_account(h, "a-tenant", "i", "tenant_admin", "tenant@acme", folder, true);

    open_environment(h);
    const ImGuiWindow* menu = GImGui->OpenPopupStack.back().Window;
    const float row = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    /* One folder heading and two accounts. */
    CHECK(menu->Size.y == Catch::Approx(2.0f * theme::menu_padding
        + 3.0f * row + 2.0f * theme::menu_row_gap).margin(1.0f));

    const auto y = find_account_row(h, "super_admin");
    REQUIRE(y.has_value());
    static_cast<void>(h.commands());
    static_cast<void>(drained_signals());
    h.click(ImVec2(menu->Pos.x + 30.0f, *y));

    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::connect_session);
    CHECK(commands[0].instance_id == "i");
    CHECK(commands[0].account_id == "a-super");
    CHECK(commands[0].principal == "super@acme");
    CHECK(commands[0].password.view().empty());
    CHECK_FALSE(commands[0].remember);

    const auto raised = drained_signals();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].detail == "Sign in to Brave Hopper as super@acme");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("an ACCOUNTS account with no memorised password asks for one",
    "[menu_bar][accounts]") {
    bar_harness h;
    add_instance(h, "i", "Brave Hopper");
    const std::string folder = privileged_folder(h, "i");
    add_account(h, "a-super", "i", "super_admin", "super@acme", folder, false);

    open_environment(h);
    const auto y = find_account_row(h, "super_admin");
    REQUIRE(y.has_value());
    static_cast<void>(h.commands());
    h.click(ImVec2(GImGui->OpenPopupStack.back().Window->Pos.x + 30.0f, *y));

    /* No sign-in yet: the form is up, and it owns who and where. */
    CHECK(h.commands().empty());
    CHECK(h.state.sign_in.open);
    CHECK(h.state.sign_in.instance_id == "i");
    CHECK(h.state.sign_in.account_id == "a-super");
    CHECK(h.state.sign_in.principal == "super@acme");
    REQUIRE(GImGui->OpenPopupStack.Size == 1);
    h.settle();

    ImGui::GetIO().AddInputCharactersUTF8("s3cret");
    h.settle();
    const ImGuiWindow* form =
        ImGui::FindWindowByName("Sign in###hedgr-menu-sign-in");
    REQUIRE(form != nullptr);
    const ImGuiID target = ImHashStr("Sign in", 0, form->ID);
    const auto submit = find_popup_row(h, form->Pos.x + 40.0f, target);
    REQUIRE(submit.has_value());
    static_cast<void>(h.commands());
    h.click(ImVec2(form->Pos.x + 40.0f, *submit));

    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::connect_session);
    CHECK(commands[0].instance_id == "i");
    CHECK(commands[0].account_id == "a-super");
    CHECK(commands[0].password.view() == "s3cret");
    CHECK_FALSE(h.state.sign_in.open);
}

TEST_CASE("ACCOUNTS with no accounts at all says so in a disabled row",
    "[menu_bar][accounts]") {
    bar_harness h;
    h.open_menu("Accounts");
    const ImGuiWindow* menu = GImGui->OpenPopupStack.back().Window;
    const float row = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    CHECK(menu->Size.y == Catch::Approx(2.0f * theme::menu_padding + row)
        .margin(1.0f));
    h.click(bar_harness::row_centre(0));
    h.settle();
    CHECK(GImGui->OpenPopupStack.Size == 1);
    CHECK(h.commands().empty());
}

TEST_CASE("VIEW: Windows is disabled while no window is open",
    "[menu_bar][view][windows]") {
    bar_harness h;
    h.open_menu("View");
    h.click(bar_harness::row_centre(4));
    h.settle();
    CHECK(GImGui->OpenPopupStack.Size == 1);
}

TEST_CASE("VIEW > Windows raises the window its row names",
    "[menu_bar][view][windows]") {
    bar_harness h;
    static_cast<void>(h.desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "One", .series_id = "s1"},
        "w-one"));
    static_cast<void>(h.desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "Two", .series_id = "s2"},
        "w-two"));
    open_windows_list(h);
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    const ImGuiWindow* child = popup_child();
    REQUIRE(child != nullptr);
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    const ImGuiID target =
        ImHashStr("##raise", 0, ImHashStr("w-two", 0, child->ID));
    const auto y = find_popup_row(h, panel->Pos.x + 30.0f, target);
    REQUIRE(y.has_value());
    h.click(ImVec2(panel->Pos.x + 30.0f, *y));
    CHECK(h.result.focus_window == "w-two");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("VIEW > Windows closes a window through its x",
    "[menu_bar][view][windows]") {
    bar_harness h;
    static_cast<void>(h.desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "One", .series_id = "s1"},
        "w-one"));
    open_windows_list(h);
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    const ImGuiWindow* child = popup_child();
    REQUIRE(child != nullptr);
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    const ImGuiID target = ImHashStr("x", 0, ImHashStr("w-one", 0, child->ID));
    const float x = panel->Pos.x + panel->Size.x - theme::menu_padding
        - ImGui::GetFrameHeight() * 0.5f;
    const auto y = find_popup_row(h, x, target);
    REQUIRE(y.has_value());
    static_cast<void>(h.commands());
    h.click(ImVec2(x, *y));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].kind == model::command::command_kind::close_window);
    CHECK(commands[0].window_id == "w-one");
    CHECK(GImGui->OpenPopupStack.Size == 0);
}

TEST_CASE("the status strip reads the connection state of the sessions",
    "[menu_bar][status_bar]") {
    headless_imgui imgui;
    theme::apply_theme();
    desk desk;
    auto input = desk.input();
    widget::status_bar status;
    const auto draw = [&] {
        ImGui::NewFrame();
        render::draw_status_bar(input, status);
        ImGui::Render();
    };
    draw();
    draw();
    const ImGuiWindow* strip = ImGui::FindWindowByName("###hedgr-status-bar");
    REQUIRE(strip != nullptr);
    CHECK(strip->Size.y == Catch::Approx(theme::status_bar_height));
    CHECK(strip->Pos.y + strip->Size.y
        == Catch::Approx(ImGui::GetMainViewport()->Size.y));
    CHECK(vertices_of(theme::status_colour(
              model::domain::session_status::authenticated)) > 0);

    desk.sessions.set_status(desk.session,
        model::domain::session_status::failed);
    draw();
    draw();
    CHECK(vertices_of(theme::failure_colour) > 0);

    /* A wall with no session at all says so in the quiet colour. */
    desk.sessions.clear();
    draw();
    draw();
    CHECK(vertices_of(u32(theme::dim_text())) > 0);
}

TEST_CASE("the status strip carries the message ladder in its severity's colour",
    "[menu_bar][status_bar]") {
    headless_imgui imgui;
    theme::apply_theme();
    desk desk;
    desk.sessions.set_status(desk.session,
        model::domain::session_status::disconnected);
    auto input = desk.input();
    input.now_ns = 1000;
    widget::status_bar status;
    const auto draw = [&] {
        ImGui::NewFrame();
        render::draw_status_bar(input, status);
        ImGui::Render();
    };

    status.show_error("the store refused the write", 1000);
    draw();
    CHECK(vertices_of(theme::failure_colour) > 0);

    status.clear_error();
    status.show_condition("still reading the deployment", 2000,
        widget::status_severity::problem);
    input.now_ns = 2000;
    draw();
    CHECK(vertices_of(theme::warning_colour) > 0);
}

TEST_CASE("a runtime failure is drawn once, in the strip and not in a window's "
          "foot",
    "[menu_bar][status_bar][workspace]") {
    headless_imgui imgui;
    theme::apply_theme();
    desk desk;
    static_cast<void>(desk.open(model::domain::window_kind::instance_manager,
        model::domain::notice_state{}, "instance-manager"));
    render::workspace workspace;
    auto input = desk.input();
    input.now_ns = 1000;
    input.runtime_error = "one unique failure the store refused";

    /*
     * The window foots draw text through ImGui, which the log buffer records;
     * the strip draws on the window's draw list, which it does not. So the
     * message being absent from the log is the foots not saying it, and the
     * strip's red vertices are the strip still saying it.
     */
    std::string text;
    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        workspace.render(input);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::Render();
    }
    CHECK(text.find("one unique failure the store refused") == std::string::npos);

    const ImGuiWindow* strip = ImGui::FindWindowByName("###hedgr-status-bar");
    REQUIRE(strip != nullptr);
    std::size_t red = 0;
    for (const ImDrawVert& vertex : strip->DrawList->VtxBuffer)
        red += vertex.col == theme::failure_colour ? 1 : 0;
    CHECK(red > 0);
}

TEST_CASE("VIEW > Windows closes every mini plot at once",
    "[menu_bar][view][windows]") {
    bar_harness h;
    static_cast<void>(h.desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "One", .series_id = "s1"},
        "m-one"));
    static_cast<void>(h.desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "Two", .series_id = "s2"},
        "m-two"));
    open_windows_list(h);
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    const ImGuiWindow* child = popup_child();
    REQUIRE(child != nullptr);
    const ImGuiWindow* panel = GImGui->OpenPopupStack.back().Window;
    const ImGuiID target = ImHashStr("##row", 0,
        ImHashStr("Close all mini plots", 0, child->ID));
    const auto y = find_popup_row(h, panel->Pos.x + 30.0f, target);
    REQUIRE(y.has_value());
    static_cast<void>(h.commands());
    h.click(ImVec2(panel->Pos.x + 30.0f, *y));
    const auto commands = h.commands();
    REQUIRE(commands.size() == 2);
    CHECK(commands[0].kind == model::command::command_kind::close_window);
    CHECK(commands[0].window_id == "m-one");
    CHECK(commands[1].window_id == "m-two");
}
