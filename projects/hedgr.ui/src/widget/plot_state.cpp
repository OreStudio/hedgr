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

/** @brief The stroke weight of a watermark's line work, as a share of its size. */
constexpr float watermark_stroke = 0.07f;

/**
 * @brief A broadcast mark with a slash through it, for a plot with no session.
 *
 * Three arcs and a dot, struck through. Built from draw primitives, like the
 * shut store's padlock, because an icon is a bitmap cell at the base text size
 * and asked for at several times that draws the atlas's missing-glyph mark.
 */
void draw_disconnected_mark(ImDrawList* draw, ImVec2 centre, float size,
    ImU32 colour) {
    const float half = size * 0.5f;
    const ImVec2 base(centre.x, centre.y + half * 0.55f);
    const float stroke = size * watermark_stroke;
    constexpr float pi = 3.14159265f;
    for (const float radius : {0.42f, 0.80f, 1.18f}) {
        draw->PathArcTo(base, half * radius, -pi * 0.75f, -pi * 0.25f, 24);
        draw->PathStroke(colour, ImDrawFlags_None, stroke);
    }
    draw->AddCircleFilled(base, size * 0.055f, colour, 16);
    draw->AddLine(ImVec2(centre.x - half * 0.78f, centre.y - half * 0.80f),
        ImVec2(centre.x + half * 0.78f, centre.y + half * 0.80f), colour,
        stroke * 1.25f);
}

/** @brief A ring with an exclamation mark, for a plot whose read failed. */
void draw_error_mark(ImDrawList* draw, ImVec2 centre, float size,
    ImU32 colour) {
    const float half = size * 0.5f;
    const float stroke = size * watermark_stroke;
    draw->AddCircle(centre, half * 0.96f, colour, 64, stroke);
    const float bar = size * 0.045f;
    draw->AddRectFilled(ImVec2(centre.x - bar, centre.y - half * 0.52f),
        ImVec2(centre.x + bar, centre.y + half * 0.10f), colour, bar);
    draw->AddCircleFilled(ImVec2(centre.x, centre.y + half * 0.45f),
        size * 0.06f, colour, 16);
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
        return;
    }

    const float extent = std::min(max.x - min.x, max.y - min.y);
    const float size = std::max(glyph_size,
        std::clamp(extent * theme::plot_watermark_share,
            theme::plot_watermark_min, theme::plot_watermark_max));
    if (state == plot_state::disconnected)
        draw_disconnected_mark(draw, centre, size,
            theme::plot_disconnected_colour);
    else
        draw_error_mark(draw, centre, size, theme::plot_failed_colour);

    if (captioned && ImGui::IsWindowHovered()
        && ImGui::IsMouseHoveringRect(min, max)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(caption_of(state));
        if (!reason.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(reason.data(), reason.data() + reason.size());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
}

}
