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
#include "hedgr.ui/widget/plot_state.hpp"

#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace hedgr::ui::widget {

namespace {

namespace theme = hedgr::ui::theme;
using hedgr::model::domain::session_status;

[[nodiscard]] const char* caption_of(plot_state state) noexcept {
    switch (state) {
    case plot_state::reading:
        return "Reading the series...";
    case plot_state::history_unavailable:
        return "History unavailable";
    default:
        return "Not connected";
    }
}

/** @brief Writes @p text dim and centred on @p x, wrapped within @p wrap. */
void dim_caption(float x, float y, float wrap, std::string_view text) {
    const char* const end = text.data() + text.size();
    const ImVec2 size = ImGui::CalcTextSize(text.data(), end, false, wrap);
    const float left = x - size.x * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(left, y));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
    ImGui::PushTextWrapPos(left + wrap);
    ImGui::TextUnformatted(text.data(), end);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}

plot_state plot_state_for(session_status status, bool reading) noexcept {
    switch (status) {
    case session_status::disconnected:
        return plot_state::disconnected;
    case session_status::failed:
        return plot_state::failed;
    case session_status::connecting:
        return plot_state::reading;
    case session_status::authenticated:
        return reading ? plot_state::reading : plot_state::none;
    }
    return plot_state::none;
}

void draw_spinner(ImDrawList* draw, ImVec2 centre, float radius) {
    constexpr float two_pi = 6.2831853f;
    const float turn = static_cast<float>(
        std::fmod(ImGui::GetTime() / theme::plot_spinner_turn_s, 1.0));
    const float start = turn * two_pi;
    draw->PathArcTo(centre, radius, start, start + theme::plot_spinner_arc,
        theme::plot_spinner_segments);
    draw->PathStroke(theme::plot_spinner_colour, ImDrawFlags_None,
        theme::plot_spinner_weight);
}

void draw_plot_state(plot_state state, ImVec2 min, ImVec2 max,
    float glyph_size, bool captioned, std::string_view reason) {
    if (state == plot_state::none)
        return;
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    if (state == plot_state::reading) {
        draw_spinner(draw, centre, glyph_size * 0.5f);
    } else {
        const char* const plug = icon::session_failed;
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(
            glyph_size, FLT_MAX, 0.0f, plug);
        draw->AddText(ImGui::GetFont(), glyph_size,
            ImVec2(std::floor(centre.x - size.x * 0.5f),
                std::floor(centre.y - size.y * 0.5f)),
            state == plot_state::failed
                    || state == plot_state::history_unavailable
                ? theme::plot_failed_colour
                                        : theme::plot_disconnected_colour,
            plug);
    }
    if (!captioned)
        return;

    const float wrap = (max.x - min.x) * theme::plot_state_caption_share;
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    float y = centre.y + glyph_size * 0.5f + gap;
    dim_caption(centre.x, y, wrap, caption_of(state));
    if (!reason.empty()) {
        y += ImGui::GetTextLineHeightWithSpacing();
        dim_caption(centre.x, y, wrap, reason);
    }
}

}
