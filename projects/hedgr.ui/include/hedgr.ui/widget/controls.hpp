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

#include "hedgr.ui/icon/icons.hpp"
#include "imgui.h"
#include <string>
#include <string_view>

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

/**
 * @brief Puts @p value on the system clipboard, and says whether it did.
 *
 * The clipboard is ImGui's, which its backend backs with the window system's,
 * so what is copied here is what another program pastes. An empty value copies
 * nothing, which is what keeps a copy of an absent field from silently
 * replacing whatever the operator already had.
 *
 * The two lines of a read-only field that cannot be selected come to this: a
 * person diagnosing a deployment wants a host name or a failure sentence in a
 * terminal, and the buffer has to leave the process for that to happen.
 */
[[nodiscard]] inline bool copy_to_clipboard(std::string_view value) {
    if (value.empty())
        return false;
    ImGui::SetClipboardText(std::string{value}.c_str());
    return true;
}

/**
 * @brief A compact control that copies one read-only value.
 *
 * Smaller than the toolbar's icons, because it sits inside a details row whose
 * height belongs to the value's text and a full frame height button would make
 * every row as tall as a toolbar. The glyph is the copy drawing the catalogue
 * keeps, reused under its Hedgr name rather than given a new name, since one
 * picture for one action is what the icon set is for.
 *
 * The tooltip names the value as well as the action. A column of identical
 * glyphs says nothing about which of them copies which field, and this pane has
 * more than one.
 *
 * @param id ImGui id, unique among the controls of one window.
 * @param value The value to copy, which is left alone when empty.
 * @param name What the value is called, for the tooltip.
 * @return Whether the control was pressed and the copy made.
 */
[[nodiscard]] inline bool copy_button(const char* id, std::string_view value,
    const char* name) {
    if (value.empty())
        return false;

    ImGui::PushID(id);
    const bool pressed = ImGui::SmallButton(icon::duplicate);
    ImGui::PopID();
    ImGui::SetItemTooltip("Copy %s", name);
    return pressed && copy_to_clipboard(value);
}

}

#endif
