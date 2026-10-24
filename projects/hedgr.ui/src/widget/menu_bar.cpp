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
#include "hedgr.ui/widget/menu_bar.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>

namespace hedgr::ui::widget {

namespace theme = hedgr::ui::theme;

bool menu_bar_entry(menu_bar_state& state, int entry, const char* popup_id,
    bool pressed) {
    const bool alive = state.open == entry && ImGui::IsPopupOpen(popup_id);
    const bool hovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    state.open = menu_after_entry(state.open, entry, alive, pressed, hovered);

    const bool now_open = state.open == entry;
    if (now_open) {
        /* Under the entry, flush with its left edge, a gap below the plate. */
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x,
            ImGui::GetItemRectMax().y + theme::menu_popup_gap));
        if (!alive)
            ImGui::OpenPopup(popup_id);
    }
    /*
     * A menu that is no longer the open one is not submitted, and ImGui drops
     * a popup that was not submitted for a frame, so there is nothing to close.
     */
    return now_open;
}

ImVec2 place_menu_panel(ImVec2 min, ImVec2 max, ImVec2 preferred) {
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const float top = max.y + theme::menu_popup_gap;
    const float margin = theme::menu_bar_margin;
    const float room_x = viewport->WorkSize.x - 2.0f * margin;
    const float room_y = viewport->WorkPos.y + viewport->WorkSize.y - top - margin;
    const ImVec2 size(std::max(0.0f, std::min(preferred.x, room_x)),
        std::max(0.0f, std::min(preferred.y, room_y)));
    const float left = menu_panel_left(min.x, size.x,
        viewport->WorkPos.x + margin,
        viewport->WorkPos.x + viewport->WorkSize.x - margin);
    ImGui::SetNextWindowPos(ImVec2(left, top));
    return size;
}

}
