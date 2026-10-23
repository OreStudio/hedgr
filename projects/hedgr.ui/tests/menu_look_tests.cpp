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
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <vector>

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::headless_imgui;

ImU32 u32(ImVec4 value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

enum row_index : std::size_t { plain, chosen, locked, more, keyed, row_count };

/** @brief An open menu popup holding one row of each kind, in a themed headless frame. */
struct menu_panel final {
    headless_imgui imgui;
    std::array<ImRect, row_count> rects{};
    std::array<bool, row_count> pressed{};
    ImRect panel;
    std::vector<ImDrawVert> verts;
    const char* heading = "files";
    bool with_separator = false;
    bool opened = false;

    menu_panel() {
        theme::apply_theme();
    }

    void frame() {
        ImGui::NewFrame();
        if (!opened) {
            ImGui::OpenPopup("menu");
            opened = true;
        }
        ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
        {
            const widget::menu_popup popup("menu");
            if (popup) {
                widget::menu_heading(heading);
                const std::array<widget::menu_row, row_count> rows{{
                    {.label = "Open"},
                    {.label = "Pick", .selected = true},
                    {.label = "Locked", .enabled = false},
                    {.label = "More", .submenu = true},
                    {.label = "Save", .shortcut = "Ctrl+S"},
                }};
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    pressed[i] = widget::menu_item_row(rows[i]);
                    rects[i] = ImRect(ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax());
                    if (with_separator && i == 1)
                        widget::menu_separator();
                }
                const auto& buffer = ImGui::GetWindowDrawList()->VtxBuffer;
                verts.assign(buffer.begin(), buffer.end());
                const ImVec2 at = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                panel = ImRect(at, ImVec2(at.x + size.x, at.y + size.y));
            }
        }
        ImGui::Render();
    }

    void settle() {
        for (int i = 0; i < 4; ++i)
            frame();
    }

    void hover(std::size_t row) {
        const ImRect& box = rects[row];
        ImGui::GetIO().AddMousePosEvent((box.Min.x + box.Max.x) * 0.5f,
            (box.Min.y + box.Max.y) * 0.5f);
        settle();
    }

    /**
     * @brief Whether a vertex of @p col lies in @p box, grown sideways by @p slack.
     *
     * Rows abut, so the bottom edge belongs to the row below.
     */
    [[nodiscard]] bool has(const ImRect& box, ImU32 col, float slack = 1.0f) const {
        return std::ranges::any_of(verts, [&](const ImDrawVert& v) {
            return v.col == col && v.pos.x >= box.Min.x - slack
                && v.pos.x <= box.Max.x + slack && v.pos.y >= box.Min.y
                && v.pos.y < box.Max.y;
        });
    }

    [[nodiscard]] ImRect right_end(std::size_t row, float width) const {
        return ImRect(rects[row].Max.x - width, rects[row].Min.y,
            rects[row].Max.x, rects[row].Max.y);
    }
};

}

TEST_CASE("an idle row has no fill and is lit by nothing", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    for (const ImVec4 fill : {theme::menu_hover_colour(),
             theme::menu_active_colour()})
        CHECK_FALSE(menu.has(menu.rects[plain], u32(fill)));
    CHECK(menu.has(menu.rects[plain], ImGui::GetColorU32(ImGuiCol_Text)));
}

TEST_CASE("a row under the pointer takes the hover tint and its neighbours do not",
    "[look][menu]") {
    menu_panel menu;
    menu.settle();
    menu.hover(plain);
    CHECK(menu.has(menu.rects[plain], u32(theme::menu_hover_colour())));
    CHECK_FALSE(menu.has(menu.rects[more], u32(theme::menu_hover_colour())));
    CHECK_FALSE(menu.has(menu.rects[plain], u32(theme::menu_selected_colour())));
}

TEST_CASE("the chosen row is the purple bar, hovered or not", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    CHECK(menu.has(menu.rects[chosen], u32(theme::menu_selected_colour())));
    CHECK_FALSE(menu.has(menu.rects[plain], u32(theme::menu_selected_colour())));
    menu.hover(chosen);
    CHECK(menu.has(menu.rects[chosen], u32(theme::menu_selected_colour())));
    CHECK_FALSE(menu.has(menu.rects[chosen], u32(theme::menu_hover_colour())));
}

TEST_CASE("the chosen bar's corners are rounded", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    const ImRect box = menu.rects[chosen];
    const ImU32 fill = u32(theme::menu_selected_colour());
    float nearest = 1.0e9f;
    for (const ImDrawVert& v : menu.verts) {
        if (v.col == fill)
            nearest = std::min(nearest,
                std::hypot(v.pos.x - box.Min.x, v.pos.y - box.Min.y));
    }
    CHECK(nearest > 1.5f);
}

TEST_CASE("a disabled row is dimmed and takes no hover", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    menu.hover(locked);
    CHECK_FALSE(menu.has(menu.rects[locked], u32(theme::menu_hover_colour())));
    CHECK(menu.has(menu.rects[locked], ImGui::GetColorU32(ImGuiCol_TextDisabled)));
    CHECK_FALSE(menu.has(menu.rects[locked], ImGui::GetColorU32(ImGuiCol_Text)));
}

TEST_CASE("a submenu row ends in a chevron and the others do not", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    const ImU32 dim = u32(theme::dim_text());
    const float reach = theme::menu_row_padding_x + 2.0f;
    CHECK(menu.has(menu.right_end(more, reach), dim, 0.0f));
    CHECK_FALSE(menu.has(menu.right_end(plain, reach), dim, 0.0f));
}

TEST_CASE("a row's shortcut text is drawn at its right edge, dim", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    const ImU32 dim = u32(theme::dim_text());
    const float half = (menu.rects[keyed].Max.x - menu.rects[keyed].Min.x) * 0.5f;
    CHECK(menu.has(menu.right_end(keyed, half), dim, 0.0f));
    CHECK_FALSE(menu.has(menu.right_end(plain, half), dim, 0.0f));
}

TEST_CASE("a menu is the flat dark panel with rounded corners and an outline",
    "[look][menu]") {
    menu_panel menu;
    menu.settle();
    CHECK(ImGui::GetStyle().PopupRounding == theme::menu_rounding);
    const ImU32 fill = u32(theme::menu_surface());
    CHECK(menu.has(menu.panel, fill));
    CHECK(menu.has(menu.panel, u32(theme::menu_border())));
    float nearest = 1.0e9f;
    for (const ImDrawVert& v : menu.verts) {
        if (v.col == fill)
            nearest = std::min(nearest,
                std::hypot(v.pos.x - menu.panel.Min.x, v.pos.y - menu.panel.Min.y));
    }
    CHECK(nearest > 2.0f);
    CHECK(menu.panel.GetWidth() >= theme::menu_min_width);
}

TEST_CASE("a separator draws a rule inside the panel", "[look][menu]") {
    const auto interior_rules = [](bool separator) {
        menu_panel menu;
        menu.with_separator = separator;
        menu.settle();
        const ImRect inner(menu.panel.Min.x + 3.0f, menu.panel.Min.y + 3.0f,
            menu.panel.Max.x - 3.0f, menu.panel.Max.y - 3.0f);
        return menu.has(inner, u32(theme::menu_border()), 0.0f);
    };
    CHECK_FALSE(interior_rules(false));
    CHECK(interior_rules(true));
}

TEST_CASE("a heading is drawn in upper case whatever its case", "[look][menu]") {
    const auto drawn = [](const char* heading) {
        menu_panel menu;
        menu.heading = heading;
        menu.settle();
        return menu.verts;
    };
    const auto lower = drawn("files");
    const auto upper = drawn("FILES");
    REQUIRE(lower.size() == upper.size());
    bool same = true;
    for (std::size_t i = 0; i < lower.size(); ++i)
        same = same && lower[i].pos.x == upper[i].pos.x
            && lower[i].uv.x == upper[i].uv.x && lower[i].uv.y == upper[i].uv.y;
    CHECK(same);
}

TEST_CASE("a top level label lights while hovered or open", "[look][menu]") {
    headless_imgui imgui;
    theme::apply_theme();
    ImRect box;
    const auto draw = [&](bool open) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 60.0f));
        ImGui::Begin("bar", nullptr, ImGuiWindowFlags_NoDecoration);
        widget::menu_label("File", open);
        box = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::End();
        ImGui::Render();
    };
    const auto lit = [&](ImU32 col) {
        const auto& buffer = ImGui::FindWindowByName("bar")->DrawList->VtxBuffer;
        return std::ranges::any_of(buffer, [&](const ImDrawVert& v) {
            return v.col == col && v.pos.x >= box.Min.x && v.pos.x <= box.Max.x;
        });
    };
    for (int i = 0; i < 3; ++i)
        draw(false);
    CHECK(lit(u32(theme::dim_text())));
    CHECK_FALSE(lit(ImGui::GetColorU32(ImGuiCol_Text)));
    draw(true);
    CHECK(lit(ImGui::GetColorU32(ImGuiCol_Text)));
    draw(false);
    ImGui::GetIO().AddMousePosEvent((box.Min.x + box.Max.x) * 0.5f,
        (box.Min.y + box.Max.y) * 0.5f);
    for (int i = 0; i < 3; ++i)
        draw(false);
    CHECK(lit(ImGui::GetColorU32(ImGuiCol_Text)));
}

TEST_CASE("a click activates a row and a disabled row ignores it", "[look][menu]") {
    menu_panel menu;
    menu.settle();
    const auto click = [&](std::size_t row) {
        menu.hover(row);
        bool seen = false;
        for (const bool down : {true, false}) {
            ImGui::GetIO().AddMouseButtonEvent(0, down);
            menu.frame();
            seen = seen || menu.pressed[row];
        }
        return seen;
    };
    CHECK(click(plain));
    CHECK_FALSE(click(locked));
}

TEST_CASE("the arrow keys reach every enabled row and skip the disabled one",
    "[look][menu][nav]") {
    menu_panel menu;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    menu.settle();
    const auto tap = [&](ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        menu.frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        menu.frame();
    };
    /* The popup opens with its first row focused, so two steps pass the disabled one. */
    tap(ImGuiKey_DownArrow);
    tap(ImGuiKey_DownArrow);
    tap(ImGuiKey_Enter);
    INFO("pressed: " << menu.pressed[0] << menu.pressed[1] << menu.pressed[2]
        << menu.pressed[3] << menu.pressed[4]);
    CHECK(menu.pressed[more]);
    CHECK_FALSE(menu.pressed[locked]);
}
