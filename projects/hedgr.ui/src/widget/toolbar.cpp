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
#include "hedgr.ui/widget/toolbar.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace hedgr::ui::widget {

namespace theme = hedgr::ui::theme;

bool toolbar_enabled(toolbar_action action,
    toolbar_availability available) noexcept {
    switch (action) {
    case toolbar_action::series_browser:
    case toolbar_action::accounts:
        return available.session;
    case toolbar_action::undock_all:
        return available.docked;
    }
    return false;
}

ImVec2 toolbar_size() noexcept {
    const float count = static_cast<float>(toolbar_action_count);
    return ImVec2(2.0f * theme::toolbar_pad_x + theme::toolbar_grip_width
            + count * (theme::toolbar_gap + theme::toolbar_button_side),
        theme::toolbar_button_side + 2.0f * theme::toolbar_pad_y);
}

toolbar_rect toolbar_grip_rect(ImVec2 origin) noexcept {
    const ImVec2 min(origin.x + theme::toolbar_pad_x,
        origin.y + theme::toolbar_pad_y);
    return {min, ImVec2(min.x + theme::toolbar_grip_width,
                     min.y + theme::toolbar_button_side)};
}

toolbar_rect toolbar_button_rect(ImVec2 origin, std::size_t index) noexcept {
    const float pitch = theme::toolbar_gap + theme::toolbar_button_side;
    const ImVec2 min(origin.x + theme::toolbar_pad_x
            + theme::toolbar_grip_width + theme::toolbar_gap
            + static_cast<float>(index) * pitch,
        origin.y + theme::toolbar_pad_y);
    return {min, ImVec2(min.x + theme::toolbar_button_side,
                     min.y + theme::toolbar_button_side)};
}

ImVec2 toolbar_clamp(ImVec2 pos, ImVec2 size, ImVec2 work_pos,
    ImVec2 work_size) noexcept {
    const float margin = theme::toolbar_edge_margin;
    const float low_x = work_pos.x + margin;
    const float high_x = std::max(low_x, work_pos.x + work_size.x - size.x - margin);
    const float high_y = std::max(work_pos.y,
        work_pos.y + work_size.y - size.y - margin);
    return ImVec2(std::clamp(pos.x, low_x, high_x),
        std::clamp(pos.y, work_pos.y, high_y));
}

ImVec2 toolbar_default_pos(ImVec2 size, ImVec2 work_pos,
    ImVec2 work_size) noexcept {
    return toolbar_clamp(
        ImVec2(work_pos.x + std::floor((work_size.x - size.x) * 0.5f),
            work_pos.y + theme::toolbar_top_gap),
        size, work_pos, work_size);
}

bool icon_tile(const char* id, const char* glyph, const char* tooltip,
    ImVec2 origin, float side, bool enabled) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(glyph, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImGui::PopID();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip
            | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + side, origin.y + side);
    if (hovered)
        draw->AddRectFilled(origin, max,
            ImGui::GetColorU32(ImGuiCol_ButtonHovered),
            ImGui::GetStyle().FrameRounding);
    const ImVec2 size = ImGui::CalcTextSize(glyph);
    const ImU32 colour = enabled
        ? ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled)
        : ImGui::GetColorU32(ImGuiCol_TextDisabled,
              ImGui::GetStyle().DisabledAlpha);
    draw->AddText(ImVec2(origin.x + std::floor((side - size.x) * 0.5f),
                       origin.y + std::floor((side - size.y) * 0.5f)),
        colour, glyph);
    return pressed;
}

}
