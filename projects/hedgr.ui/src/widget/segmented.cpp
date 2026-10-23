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
#include "hedgr.ui/widget/segmented.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace hedgr::ui::widget {

namespace {

[[nodiscard]] float segment_width(const char* label) {
    return ImGui::CalcTextSize(label).x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
}

/** @brief One segment's button and text; true when it was pressed. */
[[nodiscard]] bool draw_segment(ImDrawList* draw, ImVec2 at, ImVec2 size,
    const char* label, bool chosen) {
    ImGui::SetCursorScreenPos(at);
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    if (chosen)
        draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
            theme::segment_selected_fill, theme::segment_rounding);
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImU32 colour = chosen || hovered
        ? ImGui::GetColorU32(ImGuiCol_Text)
        : ImGui::ColorConvertFloat4ToU32(theme::dim_text());
    draw->AddText(ImVec2(at.x + std::floor((size.x - text.x) * 0.5f),
                       at.y + std::floor((size.y - text.y) * 0.5f)),
        colour, label);
    return pressed;
}

}

float segmented_height() {
    return std::ceil(ImGui::GetFontSize() * 1.3f);
}

float segmented_width(const segmented_control& control) {
    float width = theme::segment_inset * 2.0f;
    for (int option = 0; option < control.option_count; ++option)
        width += segment_width(control.options[option]);
    return width;
}

bool draw_segmented(const segmented_control& control, ImVec2 origin) {
    if (control.selected == nullptr || control.option_count <= 0
        || control.options == nullptr)
        return false;
    *control.selected = std::clamp(*control.selected, 0,
        control.option_count - 1);

    bool changed = false;
    auto* draw = ImGui::GetWindowDrawList();
    const float height = segmented_height();
    const float inner = height - theme::segment_inset * 2.0f;
    const ImVec2 end(origin.x + segmented_width(control), origin.y + height);
    draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg),
        theme::segment_rounding);

    ImGui::PushID(control.id);
    float x = origin.x + theme::segment_inset;
    const float y = origin.y + theme::segment_inset;
    for (int option = 0; option < control.option_count; ++option) {
        const char* label = control.options[option];
        const float width = segment_width(label);
        ImGui::PushID(option);
        if (draw_segment(draw, ImVec2(x, y), ImVec2(width, inner), label,
                option == *control.selected)) {
            *control.selected = option;
            changed = true;
        }
        ImGui::SetItemTooltip("%s", control.tooltip);
        ImGui::PopID();
        x += width;
    }

    ImGui::PopID();
    return changed;
}

}
