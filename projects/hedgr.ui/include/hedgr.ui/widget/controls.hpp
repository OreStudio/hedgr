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
#include "hedgr.ui/widget/capture.hpp"
#include "imgui.h"
#include <algorithm>
#include <format>
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
 * @brief A navigation rail's entry: a glyph and the word it stands for.
 *
 * The one control the reference's left hand rail is built from. It is a
 * selectable rather than a button because a rail entry is a row a reader
 * chooses among, and ImGui's selectable is the widget that carries the chosen
 * row's fill and the hover. It spans the strip's own width so the rows line up
 * as a column instead of each being as wide as its word.
 *
 * The glyph is part of the label rather than drawn beside it, because a rail
 * entry's icon and word are one item to the pointer as well as to the eye: two
 * items on one line would give the row two hit boxes and a gap between them
 * that selects nothing.
 *
 * The height is roomier than an icon button's, which is what the reference
 * uses: a rail row is about two text heights rather than a hair over one.
 *
 * @param id ImGui id, unique among the controls of one strip.
 * @param glyph The icon, which must come from @c hedgr.ui.icon.
 * @param label The word the entry is read by.
 * @param tooltip The action's name, shown on hover.
 * @return Whether the entry was pressed this frame.
 */
[[nodiscard]] inline bool rail_button(const char* id, const char* glyph,
    const char* label, const char* tooltip, bool enabled = true) {
    constexpr float rail_row_extra = 6.0f;
    const float height = ImGui::GetFrameHeight() + rail_row_extra;

    ImGui::PushID(id);
    if (!enabled)
        ImGui::BeginDisabled();
    /*
     * Rounded here rather than in the theme, because a selectable's rounding is
     * the one style value ImGui itself warns about: contiguous selectables in a
     * popup would each round their own corners and read as beads. A rail's rows
     * are not contiguous fills, so it is the surface that can use it.
     */
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, 6.0f);
    const std::string text = std::format("{}  {}", glyph, label);
    /*
     * A zero width is the selectable's own "span what is available": its size
     * argument is read as a requested width and a zero means the row's full
     * line, so the rows of a column line up rather than each being as wide as
     * its own word.
     */
    const bool pressed = ImGui::Selectable(
        text.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
    ImGui::PopStyleVar();
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed && enabled;
}

/**
 * @brief A read-only value on one line, as text that can be selected and
 * copied.
 *
 * The card form of @c text_value. A card holds one value and gives it one line,
 * so the value is a single line input rather than a wrapping multiline one: a
 * path long enough to wrap would grow the box past the card and be clipped, and
 * a value that is clipped is a value the operator cannot check. A single line
 * input scrolls its own text instead, which costs the card nothing.
 *
 * The whole value is on the tooltip as well, because a value wider than its
 * card is otherwise unreadable except by scrolling it a character at a time.
 */
inline void text_line(const char* id, std::string_view value, float width) {
    if (value.empty())
        return;

    static thread_local std::string buffer;
    buffer.assign(value);

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    static_cast<void>(ImGui::InputText("##value", buffer.data(),
        buffer.size() + 1, ImGuiInputTextFlags_ReadOnly));
    ImGui::PopID();
    ImGui::SetItemTooltip("%.*s", static_cast<int>(value.size()), value.data());

    capture_element element;
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    element.min_x = item_min.x;
    element.min_y = item_min.y;
    element.max_x = item_max.x;
    element.max_y = item_max.y;
    element.label_min_x = item_min.x;
    element.label = "value";
    observe_capture_element(id, element);
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
 * @brief The height a read-only value needs once wrapped at @p wrap_width.
 *
 * The stock multiline input reserves the width of its own scrollbar before it
 * wraps, so the box is measured at the width the input will use rather than at
 * the pane's full width. A value that fits on one line measures as one frame
 * height, which is what lets a short field and a wrapped sentence share one
 * widget without either being given a size of its own.
 *
 * The measured height is rounded up by a pixel. ImGui counts lines with a
 * wrap rule that keeps trailing blanks while this measurement drops them, so
 * the two can disagree by a line at a boundary, and a box one line short of its
 * text grows an inner scrollbar in a pane that must never raise one. Erring
 * tall costs a sliver of blank space and never a bar.
 */
[[nodiscard]] inline float text_value_height(std::string_view value,
    float wrap_width) {
    const ImVec2 measured = ImGui::CalcTextSize(value.data(),
        value.data() + value.size(), false, wrap_width);
    return measured.y + ImGui::GetStyle().FramePadding.y * 2.0f + 1.0f;
}

/**
 * @brief A read-only value drawn as text that can be selected and copied.
 *
 * The value behaves the way text does in every other program. A drag across it
 * highlights the characters and Ctrl+C puts the selection on the system
 * clipboard, which is what the operator asked for twice and what a copy control
 * per row was a poor substitute for.
 *
 * ImGui 1.92 makes this a stock input rather than a widget of our own.
 * @c ImGuiInputTextFlags_WordWrap was added to the multiline input, and
 * @c ImGuiInputTextFlags_ReadOnly still lets the stb editor track a selection,
 * so @c InputTextMultiline with both flags selects, copies and wraps at once.
 * A single line @c InputText is the one that cannot wrap, and that is the whole
 * of the earlier claim that a read-only input could not.
 *
 * The buffer ImGui edits is this function's own and is handed back untouched.
 * Read-only mode reads the value and never writes it, so the model string stays
 * const, and a copy is needed only because ImGui wants a mutable char pointer.
 * One thread local buffer is reused rather than a fresh string per row per
 * frame, because each call finishes with it before the next one starts.
 *
 * The box is exactly as tall as the wrapped text, so it cannot scroll. A
 * multiline input taller than its text grows an inner scrollbar, and the pane
 * this sits in is one that must not raise one.
 *
 * @param id ImGui id and capture name, unique among the values of one window.
 * @param value The value to show, drawn whole and never truncated.
 * @param wrap_width The width the input wraps at, with the scrollbar allowance
 *        already taken out.
 */
inline void text_value(const char* id, std::string_view value,
    float wrap_width) {
    if (value.empty())
        return;

    static thread_local std::string buffer;
    buffer.assign(value);

    ImGui::PushID(id);
    const float height = text_value_height(value, wrap_width);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly
        | ImGuiInputTextFlags_WordWrap;
    static_cast<void>(ImGui::InputTextMultiline("##value", buffer.data(),
        buffer.size() + 1, ImVec2(-FLT_MIN, height), flags));
    ImGui::PopID();

    capture_element element;
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    element.min_x = item_min.x;
    element.min_y = item_min.y;
    element.max_x = item_max.x;
    element.max_y = item_max.y;
    element.label_min_x = item_min.x;
    element.label = "value";
    observe_capture_element(id, element);
}

/**
 * @brief The file a picture key names under the picture directory.
 *
 * The key and the asset are related by the extension alone, which is the
 * relation the loader already relies on when it takes a file's stem as the key.
 * An empty directory or an empty key has no file, and that is the same answer
 * the pane gives for both rather than a path that would name a file nobody
 * installed.
 */
[[nodiscard]] inline std::string instance_picture_path(
    std::string_view directory, std::string_view key) {
    if (directory.empty() || key.empty())
        return {};

    std::string path{directory};
    if (path.back() != '/')
        path.push_back('/');
    path.append(key.data(), key.size());
    path.append(".png");
    return path;
}

}

#endif
