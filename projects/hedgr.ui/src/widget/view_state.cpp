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
#include "hedgr.ui/widget/view_state.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace hedgr::ui::widget {

namespace {

namespace theme = hedgr::ui::theme;
using hedgr::model::domain::session_status;

/** @brief The stroke weight of a mark's line work, as a share of its size. */
constexpr float watermark_stroke = 0.07f;

/**
 * @brief Broadcast arcs with a slash through them, for no usable session.
 *
 * Built from draw primitives, like the shut store's padlock, because an icon is
 * a bitmap cell at the base text size and asked for at several times that draws
 * the atlas's missing-glyph mark.
 */
void draw_not_connected_mark(ImDrawList* draw, ImVec2 centre, float size,
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

/** @brief A ring with an exclamation mark, for something that failed. */
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

/** @brief Empty chart axes with a dashed flat line, for nothing to show. */
void draw_no_data_mark(ImDrawList* draw, ImVec2 centre, float size,
    ImU32 colour) {
    const float half = size * 0.5f;
    const float stroke = size * watermark_stroke;
    const float left = centre.x - half * 0.9f;
    const float right = centre.x + half * 0.9f;
    const float top = centre.y - half * 0.8f;
    const float bottom = centre.y + half * 0.8f;
    draw->AddLine(ImVec2(left, top), ImVec2(left, bottom), colour, stroke);
    draw->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), colour, stroke);
    constexpr int dashes = 4;
    const float run = (right - left - half * 0.2f) / (dashes * 2 - 1);
    const float y = centre.y;
    for (int i = 0; i < dashes; ++i) {
        const float x = left + half * 0.2f + run * static_cast<float>(i * 2);
        draw->AddLine(ImVec2(x, y), ImVec2(x + run, y), colour, stroke * 0.8f);
    }
}

/** @brief Draws a verdict's mark in the area, and the tooltip over it. */
void draw_into(const view_verdict& verdict, ImVec2 min, ImVec2 max,
    float glyph_size) {
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    if (verdict.state == view_state::reading) {
        draw_spinner(draw, centre, glyph_size * 0.5f);
        return;
    }

    const float extent = std::min(max.x - min.x, max.y - min.y);
    const float size = std::max(glyph_size,
        std::clamp(extent * theme::plot_watermark_share,
            theme::plot_watermark_min, theme::plot_watermark_max));
    switch (verdict.state) {
    case view_state::not_connected:
        draw_not_connected_mark(draw, centre, size, theme::view_quiet_colour);
        break;
    case view_state::error:
        draw_error_mark(draw, centre, size, theme::view_error_colour);
        break;
    case view_state::no_data:
        draw_no_data_mark(draw, centre, size, theme::view_quiet_colour);
        break;
    case view_state::none:
    case view_state::reading:
        return;
    }

    if (!ImGui::IsWindowHovered() || !ImGui::IsMouseHoveringRect(min, max))
        return;
    const auto words = tooltip_of(verdict);
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(words.caption.data(),
        words.caption.data() + words.caption.size());
    if (!words.detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(words.detail.data(),
            words.detail.data() + words.detail.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
}

/** @brief Fills the rest of the current window less @p reserved, with the mark. */
void fill_rest(const view_verdict& verdict, float reserved) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 room = ImGui::GetContentRegionAvail();
    draw_into(verdict, at,
        ImVec2(at.x + room.x,
            at.y + std::max(room.y - reserved,
                       theme::view_state_body_min_height)),
        theme::view_state_glyph_size);
}

}

view_verdict view_state_of(const view_facts& facts) noexcept {
    if (!facts.session) {
        return {view_state::not_connected,
            facts.ended ? view_words::session_ended : view_words::not_connected,
            {}};
    }
    switch (*facts.session) {
    case session_status::disconnected:
        return {view_state::not_connected, view_words::not_connected, {}};
    case session_status::connecting:
        return {view_state::reading, view_words::none, {}};
    case session_status::failed:
        return {view_state::error, view_words::connection_failed,
            facts.session_error};
    case session_status::authenticated:
        break;
    }
    if (!facts.has_token)
        return {view_state::not_connected, view_words::not_connected, {}};
    if (facts.reading)
        return {view_state::reading, view_words::none, {}};
    if (!facts.read_failure.empty())
        return {view_state::error, view_words::read_failed,
            facts.read_failure};
    if (!facts.has_data)
        return {view_state::no_data, view_words::empty, facts.empty_what};
    return {};
}

view_tooltip tooltip_of(const view_verdict& verdict) noexcept {
    switch (verdict.words) {
    case view_words::none:
        return {};
    case view_words::not_connected:
        return {"Not connected", "Sign in from the Accounts window."};
    case view_words::session_ended:
        return {"This session has ended",
            "Sign in again from the Accounts window."};
    case view_words::connection_failed:
        return {"Connection failed", verdict.reason};
    case view_words::read_failed:
        return {"Could not read the data", verdict.reason};
    case view_words::empty:
        return {"No data", verdict.reason};
    }
    return {};
}

std::string_view view_foot_word(view_state state) noexcept {
    switch (state) {
    case view_state::not_connected:
        return "OFFLINE";
    case view_state::error:
        return "ERROR";
    case view_state::no_data:
        return "NO DATA";
    case view_state::none:
    case view_state::reading:
        return {};
    }
    return {};
}

void set_view_foot(foot_state& foot, const view_verdict& verdict) {
    if (verdict.state == view_state::reading) {
        foot.updating = true;
        return;
    }
    const auto word = view_foot_word(verdict.state);
    if (word.empty())
        return;
    foot.word = std::string{word};
    foot.colour = verdict.state == view_state::error
        ? theme::failure_colour
        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (verdict.state == view_state::error)
        foot.detail = std::string{verdict.reason};
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

void draw_view_state(const view_verdict& verdict, ImVec2 min, ImVec2 max,
    float glyph_size) {
    if (verdict.state == view_state::none)
        return;
    draw_into(verdict, min, max, glyph_size);
}

void draw_view_state_body(const status_bar& status, foot_state& foot,
    const view_verdict& verdict) {
    set_view_foot(foot, verdict);
    fill_rest(verdict,
        status_foot_height(status, foot) + ImGui::GetStyle().ItemSpacing.y);
}

void draw_view_state_pane(const view_verdict& verdict) {
    fill_rest(verdict, 0.0f);
}

void draw_view_state_inline(const view_verdict& verdict, ImVec2 size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    draw_into(verdict, at, ImVec2(at.x + size.x, at.y + size.y),
        std::min(theme::view_state_card_glyph_size, std::min(size.x, size.y)));
}

}
