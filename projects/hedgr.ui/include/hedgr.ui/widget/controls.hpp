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
#ifndef HEDGR_UI_WIDGET_CONTROLS_HPP
#define HEDGR_UI_WIDGET_CONTROLS_HPP

#include "imgui.h"

namespace hedgr::ui::widget {

/**
 * @brief An icon-only button carrying a tooltip that names the action.
 *
 * The single control every action in this interface is built from. There is no
 * menu bar anywhere, so a glyph with no tooltip is a guess rather than a
 * command: the tooltip is the action's only name on screen, and it is not
 * optional.
 *
 * Sizes itself to the frame height so a toolbar's icons line up with each
 * other and with the text boxes beside them, whatever font the theme installs.
 *
 * @param id ImGui id, unique among the buttons of one window.
 * @param glyph The icon, which must come from @c hedgr.ui.icon.
 * @param tooltip The action's name, shown on hover.
 * @return Whether the button was pressed this frame.
 */
[[nodiscard]] inline bool icon_button(const char* id, const char* glyph,
    const char* tooltip, bool enabled = true) {
    const float height = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    if (!enabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(glyph, ImVec2(height, height));
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed && enabled;
}

/**
 * @brief A text box occupying everything left of a trailing control.
 *
 * The eye toggle sits in the same row as the secret it reveals, so the box has
 * to give up exactly the width of that control. Measuring it here rather than
 * at each call site is what stops the two from drifting apart.
 *
 * @param height Height of the control that follows on the same line.
 * @return The width to give the text box, as ImGui's negative reserved width.
 */
[[nodiscard]] inline float width_before(float height) noexcept {
    return -(height + ImGui::GetStyle().ItemInnerSpacing.x);
}

}

#endif
