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
#include "hedgr.ui/render/menu_bar.hpp"

#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ores.platform/process/executable.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace hedgr::ui::render {

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::model::domain::store_lock;

/** @brief The logo's place in the bar, and the id its popup opens under. */
constexpr int logo_entry = 0;
constexpr const char* system_menu_id = "system menu";
constexpr const char* logo_texture_id = "ore-logo-white";

[[nodiscard]] ImU32 u32(ImVec4 colour) {
    return ImGui::ColorConvertFloat4ToU32(colour);
}

/** @brief The logo button: a bar-high square with the swirl in it. */
bool draw_logo(const render_input& input, std::string_view logo_svg,
    bool menu_open) {
    const float side = theme::menu_bar_height;
    ImGui::PushID("logo");
    const bool pressed = ImGui::InvisibleButton("##logo", ImVec2(side, side));
    ImGui::PopID();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const bool lit = menu_open || ImGui::IsItemHovered();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    if (lit) {
        const float inset = theme::menu_bar_plate_inset;
        draw->AddRectFilled(ImVec2(min.x + inset, min.y + inset),
            ImVec2(max.x - inset, max.y - inset),
            u32(theme::menu_hover_colour()), theme::menu_row_rounding);
    }
    if (!logo_svg.empty() && input.textures != nullptr) {
        const float pad = theme::menu_bar_logo_padding;
        const float scale = std::max(1.0f,
            ImGui::GetIO().DisplayFramebufferScale.y);
        const int pixels = static_cast<int>(std::lround((side - 2.0f * pad) * scale));
        const ImTextureID texture = input.textures->svg(logo_texture_id,
            logo_svg, pixels, pixels);
        if (texture != 0)
            draw->AddImage(texture, ImVec2(min.x + pad, min.y + pad),
                ImVec2(max.x - pad, max.y - pad), ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                lit ? ImGui::GetColorU32(ImGuiCol_Text)
                    : u32(theme::dim_text()));
    }
    return pressed;
}

/**
 * @brief The rows of the system menu.
 *
 * While the store is shut the catalogue is behind the master password, so the
 * menu offers the way in and nothing else, as the shortcuts bar draws nothing
 * then.
 */
void draw_system_menu(const render_input& input, menu_bar_result& result) {
    if (shows_instances(input)) {
        if (widget::menu_item_row({.label = "Instances"})) {
            request_instances_window(input);
            result.focus_instances = true;
        }
        widget::menu_separator();
    }
    switch (input.store_lock) {
    case store_lock::unlocked:
        if (widget::menu_item_row({.label = "Lock store"}))
            result.lock_requested = true;
        break;
    case store_lock::absent:
        if (widget::menu_item_row({.label = "Create store"}))
            result.unlock_requested = true;
        break;
    case store_lock::locked:
    case store_lock::failed:
        if (widget::menu_item_row({.label = "Unlock store"}))
            result.unlock_requested = true;
        break;
    }
}

}

std::filesystem::path logo_svg_path() {
    constexpr const char* name = "ore-logo-white.svg";
    const auto self = ores::platform::process::executable_path();
    std::vector<std::filesystem::path> candidates;
    if (!self.empty()) {
        const auto exe_dir = self.parent_path();
        candidates.push_back(exe_dir.parent_path() / "share" / "hedgr.ui"
            / "branding" / name);
        candidates.push_back(exe_dir / "branding" / name);
    }
    candidates.push_back(std::filesystem::path("assets/branding") / name);

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

void draw_menu_bar(const render_input& input, std::string_view logo_svg,
    widget::menu_bar_state& state, menu_bar_result& result) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::menu_bar_surface());
    const bool visible = ImGui::BeginViewportSideBar("###hedgr-menu-bar",
        ImGui::GetMainViewport(), ImGuiDir_Up, theme::menu_bar_height, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (visible) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        const bool pressed = draw_logo(input, logo_svg,
            state.open == logo_entry);
        if (widget::menu_bar_entry(state, logo_entry, system_menu_id, pressed)) {
            const widget::menu_popup popup(system_menu_id);
            if (popup)
                draw_system_menu(input, result);
        }

        /* The hairline along the bottom edge, drawn over the fill. */
        const ImVec2 at = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + size.y - 0.5f),
            ImVec2(at.x + size.x, at.y + size.y - 0.5f),
            u32(theme::menu_border()));
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

}
