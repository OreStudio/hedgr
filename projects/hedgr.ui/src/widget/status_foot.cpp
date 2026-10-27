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

#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include <algorithm>
#include <string>

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

/** @brief The status and its timestamp, split where their colours change. */
struct status_text final {
    /** @brief The age and its separator, drawn in the state's colour. */
    std::string age;

    /** @brief The ISO instant, with the updating note after it. */
    std::string timestamp;

    /** @brief The freshness word, which is the last thing on the bar. */
    std::string word;
};

/**
 * @brief Builds the timestamp and the freshness word as one statement.
 *
 * The timestamp is the quiet text a reader checks; the dot and the word are
 * the state's colour and sit at the far right. Keeping them in one build is
 * what stops a screen from showing a state without the time it was judged on.
 */
[[nodiscard]] status_text build_status_text(const foot_state& state) {
    status_text text;
    if (!state.age.empty())
        text.age = state.age + " \u00b7 ";
    if (!state.as_of.empty())
        text.timestamp = state.as_of;
    if (state.updating)
        text.timestamp += text.timestamp.empty()
            ? "updating" : " \u00b7 updating";
    text.word = state.word;
    return text;
}

/** @brief The width a combo needs for its widest option and its arrow. */
[[nodiscard]] float combo_width(const foot_combo& control) {
    float widest = 0.0f;
    for (int i = 0; i < control.option_count; ++i)
        widest = std::max(widest,
            ImGui::CalcTextSize(control.options[i]).x);
    const ImGuiStyle& style = ImGui::GetStyle();
    return widest + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}

/** @brief The width a caption and the spacing after it take, or zero. */
[[nodiscard]] float caption_width(const foot_combo& control) {
    if (control.caption == nullptr || control.caption[0] == '\0')
        return 0.0f;
    return ImGui::CalcTextSize(control.caption).x
        + ImGui::GetStyle().ItemInnerSpacing.x;
}

/** @brief The width of every control plus the gaps between them. */
[[nodiscard]] float controls_width(const foot_state& state) {
    if (state.controls.empty())
        return 0.0f;
    float width = 0.0f;
    for (const auto& control : state.controls)
        width += caption_width(control) + combo_width(control);
    return width
        + ImGui::GetStyle().ItemSpacing.x
            * static_cast<float>(state.controls.size() - 1);
}

/**
 * @brief Draws one control at an absolute position and writes its choice.
 *
 * The option list is borrowed rather than copied. The selected index is
 * clamped before it is used, so a preference file that named a choice this
 * build no longer offers still opens on a choice that exists.
 *
 * @return whether the choice changed this frame.
 */
[[nodiscard]] bool draw_control(const foot_combo& control, float x,
    float y) {
    if (control.selected == nullptr || control.option_count <= 0
        || control.options == nullptr)
        return false;
    *control.selected = std::clamp(*control.selected, 0,
        control.option_count - 1);

    bool changed = false;
    if (control.caption != nullptr && control.caption[0] != '\0') {
        ImGui::SetCursorPos(ImVec2(x,
            y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextDisabled("%s", control.caption);
    }
    ImGui::SetCursorPos(ImVec2(x + caption_width(control), y));
    ImGui::PushID(control.id);
    ImGui::SetNextItemWidth(combo_width(control));
    if (ImGui::BeginCombo("##combo",
            control.options[*control.selected])) {
        for (int i = 0; i < control.option_count; ++i) {
            const bool selected = i == *control.selected;
            if (ImGui::Selectable(control.options[i], selected)) {
                *control.selected = i;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", control.tooltip);
    ImGui::PopID();
    return changed;
}

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
        /*
         * The glyph belongs to the base face, which is where the icon sheet is
         * merged. The foot's numeric face carries digits and prose, not icons.
         */
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextUnformatted(hedgr::ui::icon::error);
        ImGui::PopFont();
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
    if (!status.message().empty())
        ImGui::SetItemTooltip("%s", status.message().c_str());

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

}

float status_foot_height(const status_bar& status, const foot_state& state) {
    const int rows = status.severity() == status_severity::error
        ? status_error_row_lines
        : status_row_lines;
    const float text_height =
        ImGui::GetTextLineHeight() * static_cast<float>(rows) + 2.0f;
    if (state.controls.empty())
        return text_height;

    /*
     * A control is a frame, not a line of text. The foot keeps the extra height
     * only on a window that put a control there, so the tables and the lock
     * prompt keep the compact line they had.
     */
    return std::max(text_height, ImGui::GetFrameHeight() + 4.0f);
}

float status_foot_height(const status_bar& status) {
    return status_foot_height(status, foot_state{});
}

bool draw_status_foot(status_bar& status, std::int64_t now_ns,
    const foot_state& state) {
    /*
     * The foot is a fixed-width strip. Its identity, controls, timestamp and
     * state all line up from one window to the next, and a changing digit does
     * not move the word beside it.
     */
    ImGui::PushFont(hedgr::ui::icon::numeric_font());
    const float height = status_foot_height(status, state);

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
    const float gap = style.ItemSpacing.x;
    const float left = ImGui::GetCursorPosX();
    const float right = left + ImGui::GetContentRegionAvail().x;

    const auto text = build_status_text(state);
    const float dot_side = ImGui::GetTextLineHeight() * 0.55f;
    const float dot_gap = style.ItemInnerSpacing.x;
    const float age_width = text.age.empty()
        ? 0.0f : ImGui::CalcTextSize(text.age.c_str()).x;
    const float timestamp_width = age_width + (text.timestamp.empty()
        ? 0.0f : ImGui::CalcTextSize(text.timestamp.c_str()).x);
    const float word_width = text.word.empty()
        ? 0.0f : ImGui::CalcTextSize(text.word.c_str()).x;
    const float head_width = text.word.empty()
        ? 0.0f : dot_side + dot_gap + word_width;
    const bool has_both = timestamp_width > 0.0f && head_width > 0.0f;
    const float status_width = timestamp_width + head_width
        + (has_both ? gap : 0.0f);
    const float controls = controls_width(state);
    const float controls_reservation =
        controls > 0.0f ? controls + gap * 2.0f : 0.0f;
    const float reserved = status_width + controls_reservation;

    /*
     * The state and its time go against the foot's right edge, and the controls
     * sit immediately to their left. The identity is clipped short of that
     * room, because it is a fact a reader checks occasionally and the controls
     * are the ones they came to use.
     */
    const float state_x = std::max(left, right - status_width);
    const float controls_x = controls > 0.0f
        ? std::max(left, state_x - gap * 2.0f - controls)
        : state_x;
    const float identity_right = std::max(left, controls_x - gap);

    if (!status.empty()) {
        draw_status_line(status, now_ns, reserved + gap);
    } else if (!state.identity.empty() && identity_right > left) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(origin,
            ImVec2(origin.x + identity_right - left,
                origin.y + ImGui::GetTextLineHeight()),
            true);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(state.identity.c_str());
        ImGui::PopStyleColor();
        ImGui::PopClipRect();
        ImGui::SetItemTooltip("%s", state.identity.c_str());
    }

    const float line_y = (height - ImGui::GetTextLineHeight()) * 0.5f;
    const float combo_y = (height - ImGui::GetFrameHeight()) * 0.5f;

    bool changed = false;
    float x = controls_x;
    for (const auto& control : state.controls) {
        changed = draw_control(control, x, combo_y) || changed;
        x += caption_width(control) + combo_width(control) + gap;
    }

    if (!text.age.empty() || !text.timestamp.empty()) {
        ImGui::SetCursorPos(ImVec2(state_x, line_y));
        if (!text.age.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(state.colour),
                "%s", text.age.c_str());
            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::TextDisabled("%s", text.timestamp.c_str());
    }

    if (!text.word.empty()) {
        const float head_x = timestamp_width <= 0.0f
            ? state_x : state_x + timestamp_width + gap;
        ImGui::SetCursorPos(ImVec2(head_x, line_y));
        const ImVec2 dot_origin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(dot_origin.x + dot_side * 0.5f,
                dot_origin.y + ImGui::GetTextLineHeight() * 0.5f),
            dot_side * 0.5f, state.colour, 24);
        ImGui::Dummy(ImVec2(dot_side, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, dot_gap);
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(state.colour), "%s",
            text.word.c_str());
        if (!state.detail.empty())
            ImGui::SetItemTooltip("%s", state.detail.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    return changed;
}

}
