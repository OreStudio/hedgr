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
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <variant>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

void render_mini_plot(window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(state);
    static_cast<void>(status);
    static_cast<void>(lock);

    const auto& shape = input.windows.state(handle);
    const auto* card = std::get_if<model::domain::mini_plot_state>(&shape);
    if (card == nullptr)
        return;

    const auto held = input.windows.series(handle);
    const bool reading = held.empty() || !input.market.contains(held.front());

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 corner(origin.x + size.x, origin.y + size.y);
    constexpr float rounding = 8.0f;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, corner,
        ImGui::ColorConvertFloat4ToU32(theme::card_surface()), rounding);
    draw->AddRect(origin, corner,
        ImGui::ColorConvertFloat4ToU32(theme::card_border()), rounding);

    const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + pad));
    ImGui::TextUnformatted(card->label.c_str());
    if (reading) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad,
            origin.y + pad + ImGui::GetTextLineHeightWithSpacing()));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
        ImGui::TextUnformatted("Reading the series...");
        ImGui::PopStyleColor();
    }
}

}
