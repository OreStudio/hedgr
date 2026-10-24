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
#include "hedgr.ui/render/toolbar.hpp"

#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/toolbar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <array>
#include <cstddef>

namespace hedgr::ui::render {

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;

constexpr const char* toolbar_window = "###hedgr-toolbar";

struct toolbar_button final {
    widget::toolbar_action action;
    const char* id;
    const char* glyph;
    const char* tooltip;
};

constexpr std::array<toolbar_button, widget::toolbar_action_count> buttons{{
    {widget::toolbar_action::series_browser, "series", icon::chart,
        "Series browser"},
    {widget::toolbar_action::accounts, "accounts", icon::party, "Accounts"},
    {widget::toolbar_action::undock_all, "undock", icon::move, "Undock all"},
}};

[[nodiscard]] ImU32 u32(ImVec4 colour) {
    return ImGui::ColorConvertFloat4ToU32(colour);
}

/** @brief The capsule: a pill filled translucent dark with the purple outline. */
void draw_capsule(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    const float rounding = (max.y - min.y) * 0.5f;
    draw->AddRectFilled(min, max, u32(theme::toolbar_fill()), rounding);
    const float inset = theme::toolbar_outline_width * 0.5f;
    draw->AddRect(ImVec2(min.x + inset, min.y + inset),
        ImVec2(max.x - inset, max.y - inset), u32(theme::toolbar_outline()),
        rounding - inset, ImDrawFlags_None, theme::toolbar_outline_width);
}

/** @brief The six dots of the grip, two columns of three. */
void draw_grip(ImDrawList* draw, widget::toolbar_rect grip, bool lit) {
    const ImU32 colour = ImGui::GetColorU32(
        lit ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float pitch = theme::toolbar_grip_dot_pitch;
    const float cx = (grip.min.x + grip.max.x) * 0.5f;
    const float cy = (grip.min.y + grip.max.y) * 0.5f;
    for (int column = 0; column < 2; ++column) {
        for (int row = 0; row < 3; ++row)
            draw->AddCircleFilled(ImVec2(cx + (static_cast<float>(column) - 0.5f) * pitch,
                                      cy + static_cast<float>(row - 1) * pitch),
                theme::toolbar_grip_dot_radius, colour, 8);
    }
}

void press(const render_input& input, widget::toolbar_action action,
    toolbar_result& result) {
    const auto session = current_session(input);
    switch (action) {
    case widget::toolbar_action::series_browser:
        if (session)
            request_session_window(input, *session,
                hedgr::model::domain::window_kind::series_browser);
        break;
    case widget::toolbar_action::accounts:
        if (session)
            request_accounts_window(input, input.sessions.instance_id(*session));
        break;
    case widget::toolbar_action::undock_all:
        result.undock_all = true;
        break;
    }
}

}

void draw_toolbar(const render_input& input, bool desk_docked,
    toolbar_result& result) {
    if (input.store_lock != hedgr::model::domain::store_lock::unlocked
        || !input.market_view.toolbar_visible)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size = widget::toolbar_size();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(widget::toolbar_default_pos(size, viewport->WorkPos,
                                viewport->WorkSize),
        ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin(toolbar_window, nullptr, flags);
    ImGui::PopStyleVar(2);
    if (visible) {
        /*
         * Clamped after Begin so a restored or resized position is corrected in
         * the frame it is read, not the next. The window is NoMove: the grip
         * below does the dragging, so that the body of the capsule never moves
         * it by accident.
         */
        ImVec2 at = ImGui::GetWindowPos();
        const ImVec2 clamped = widget::toolbar_clamp(at, size, viewport->WorkPos,
            viewport->WorkSize);
        if (clamped.x != at.x || clamped.y != at.y) {
            ImGui::SetWindowPos(clamped);
            at = clamped;
        }

        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy(size);
        draw_capsule(ImGui::GetWindowDrawList(), at,
            ImVec2(at.x + size.x, at.y + size.y));

        const widget::toolbar_rect grip = widget::toolbar_grip_rect(at);
        ImGui::SetCursorScreenPos(grip.min);
        ImGui::InvisibleButton("##grip", ImVec2(grip.max.x - grip.min.x,
                                             grip.max.y - grip.min.y));
        const ImGuiID grip_id = ImGui::GetItemID();
        const bool grip_hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (grip_hot)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID grab_x = ImGui::GetID("grab-x");
        const ImGuiID grab_y = ImGui::GetID("grab-y");
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated()) {
            storage->SetFloat(grab_x, mouse.x - at.x);
            storage->SetFloat(grab_y, mouse.y - at.y);
        }
        if (grip_id != 0 && ImGui::IsItemActive()) {
            const ImVec2 wanted(mouse.x - storage->GetFloat(grab_x),
                mouse.y - storage->GetFloat(grab_y));
            at = widget::toolbar_clamp(wanted, size, viewport->WorkPos,
                viewport->WorkSize);
            ImGui::SetWindowPos(at);
            ImGui::MarkIniSettingsDirty();
        }
        draw_grip(ImGui::GetWindowDrawList(), widget::toolbar_grip_rect(at),
            grip_hot);

        const widget::toolbar_availability available{
            .session = current_session(input).has_value(),
            .docked = desk_docked};
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto& button = buttons[i];
            const widget::toolbar_rect rect = widget::toolbar_button_rect(at, i);
            if (widget::icon_tile(button.id, button.glyph, button.tooltip,
                    rect.min, theme::toolbar_button_side,
                    widget::toolbar_enabled(button.action, available)))
                press(input, button.action, result);
        }

        /*
         * Above the desk's windows, which a click brings forward over it, but
         * never above a popup: a menu opened from the bar drops over this
         * capsule's own place.
         */
        if (GImGui->OpenPopupStack.Size == 0)
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    ImGui::End();
}

}
