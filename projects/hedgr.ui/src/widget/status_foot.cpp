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
#include "hedgr.ui/widget/status_foot.hpp"

#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include <algorithm>

namespace hedgr::ui::widget {

namespace {

/**
 * @brief The status line's own metrics.
 *
 * One compact row, measured from the current font. The foot is chrome and must
 * not eat the plot; a long message is clipped rather than given a second row.
 */
constexpr int status_row_lines = 1;

/** @brief An error gets the same single row; its glyph and colour carry it. */
constexpr int status_error_row_lines = 1;

/**
 * @brief Draws the window's status line: the last thing that happened.
 *
 * The bar itself decides what is on it and whether its time is up; this only
 * gives it the frame's clock and the moment to age itself. Reading the clock
 * here rather than keeping one is what makes the fade frame-rate independent
 * without a timer anywhere in the interface.
 *
 * An error is drawn differently from a remark, and it is drawn differently in
 * two ways at once because the difference has to survive a glance at a line
 * that may be at the far side of the screen: a glyph in front of it and the
 * palette's red rather than the dim grey every other message uses. A remark
 * keeps the disabled text colour, because it is a thing that happened; red on
 * a fade would read as a second alarm going off.
 *
 * The line is wrapped short of the room the foot keeps for the state, so a long
 * sentence and the state never write over one another.
 */
void draw_status_line(status_bar& status, std::int64_t now_ns,
    float reserved_width) {
    status.advance(now_ns);

    const bool error = status.severity() == status_severity::error;

    /*
     * The fade is a style alpha rather than a colour of its own, so the line
     * keeps the interface's disabled text colour on the way out. A message
     * that changed colour as it went would read as a second message. An error
     * never fades, so it keeps its red for as long as it is on the bar.
     */
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, status.alpha(now_ns));

    /*
     * An error is white behind its red glyph rather than the disabled grey a
     * remark gets. Disabled grey is what an interface uses for something it is
     * not offering, and a failure the operator has to act on is the opposite
     * of that.
     */
    float glyph_width = 0.0f;
    if (error) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(hedgr::ui::theme::failure_colour));
        ImGui::TextUnformatted(hedgr::ui::icon::error);
        ImGui::PopStyleColor();
        glyph_width =
            ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(glyph_width);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    /*
     * The message is a selectable value like the pane's fields, so a failure
     * can be dragged across and copied out of the bar as well as read in it.
     * The box is given its whole wrapped height and the foot keeps its fixed
     * number of rows, so a sentence longer than the foot is clipped by the foot
     * and never given a scrollbar. That is the visible line count the drawn
     * text already had, with the selection added.
     */
    const float wrap_width = std::max(
        ImGui::GetContentRegionAvail().x - reserved_width
            - ImGui::GetStyle().ScrollbarSize,
        1.0f);
    text_value("status", status.message(), wrap_width);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

}

float status_foot_height(const status_bar& status) {
    const int rows = status.severity() == status_severity::error
        ? status_error_row_lines
        : status_row_lines;
    /* One text row and a hair of air: the foot is chrome, not content. */
    return ImGui::GetTextLineHeight() * static_cast<float>(rows) + 2.0f;
}

void draw_status_foot(status_bar& status, std::int64_t now_ns,
    const foot_state& state) {
    const float height = status_foot_height(status);

    /*
     * The reservation: an empty child of exactly the foot's height, which
     * advances the cursor past the foot and leaves the line below it with the
     * window's whole width.
     */
    ImGui::BeginChild("##hedgr-status-space", ImVec2(0.0f, height),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::EndChild();

    const float bottom = ImGui::GetWindowHeight() - height
        - ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPosY(bottom);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::Separator();
    ImGui::PopStyleVar();

    /*
     * The foot sits on the reference's status surface rather than on the
     * panel's, so the line reads as a statement about the window rather than
     * as the last row of its contents. ImGui has no status colour, which is
     * why the surface is pushed over ChildBg and popped again below.
     */
    ImGui::PushStyleColor(ImGuiCol_ChildBg, hedgr::ui::theme::status_surface());
    /*
     * The line child asks for its padding rather than getting it free. A child
     * with no border is given none, and without this the message sits flush
     * against the separator above it no matter how much height the foot
     * reserved, because all of that air falls below the text.
     */
    ImGui::BeginChild("##hedgr-status-line", ImVec2(0.0f, height),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    const ImGuiStyle& style = ImGui::GetStyle();
    const bool has_state = !state.word.empty();
    const float gap = style.ItemSpacing.x;

    /*
     * The disc is the state's colour and belongs with the state's word, so both
     * live at the right end of the foot rather than one at each end. Its
     * diameter is one text line, twice the half line it was, which is large
     * enough to read at a glance and still inside the foot: the foot reserves
     * two text rows and the disc is centred on the first of them, so it spans
     * that row exactly and is never clipped.
     */
    const float dot_side = ImGui::GetTextLineHeight();
    const float state_width = !has_state
        ? 0.0f
        : dot_side + gap + ImGui::CalcTextSize(state.word.c_str()).x;

    /*
     * The state is put against the foot's right edge, and the message is
     * wrapped short of the room it takes, so the two share the bar without
     * either being written over. The disc and its gap are counted in that room
     * as well, so the message stops before the colour as it stops before the
     * words.
     */
    const float state_y = ImGui::GetCursorPosY();
    const float state_x = ImGui::GetCursorPosX()
        + ImGui::GetContentRegionAvail().x - state_width;
    if (!status.empty()) {
        draw_status_line(status, now_ns, state_width);
    } else if (!state.identity.empty()) {
        /*
         * The identity is clipped rather than wrapped: a long principal and a
         * long party name on a narrow window would take the second row and
         * change the foot's height, and the foot never changes height.
         */
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float right = ImGui::GetWindowPos().x
            + ImGui::GetWindowContentRegionMax().x - state_width;
        ImGui::PushClipRect(origin,
            ImVec2(right, origin.y + ImGui::GetTextLineHeight()), true);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(state.identity.c_str());
        ImGui::PopStyleColor();
        ImGui::PopClipRect();
    }

    if (has_state) {
        ImGui::SetCursorPos(ImVec2(state_x, state_y));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float centre_y = origin.y + ImGui::GetTextLineHeight() * 0.5f;
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(origin.x + dot_side * 0.5f, centre_y), dot_side * 0.5f,
            state.colour, 32);

        ImGui::SetCursorPosX(state_x + dot_side + gap);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(state.colour));
        ImGui::TextUnformatted(state.word.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}
