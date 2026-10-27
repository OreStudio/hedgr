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
#include "hedgr.ui/widget/menu.hpp"

#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace hedgr::ui::widget {

namespace {

constexpr int style_vars_pushed = 3;
constexpr int style_colours_pushed = 5;

/** @brief The byte length of the UTF-8 character at @p text, never past @p end. */
[[nodiscard]] int char_length(const char* text, const char* end) {
    unsigned int ignored = 0;
    return std::max(1, ImTextCharFromUtf8(&ignored, text, end));
}

/**
 * @brief Draws @p text in upper case, letter-spaced, and returns its width.
 *
 * One character at a time through a one character stack buffer, so the frame
 * allocates nothing. Only ASCII is folded; other bytes pass through as they
 * are. A null @p draw only measures.
 */
float draw_upper(ImDrawList* draw, ImVec2 at, const char* text, ImU32 colour) {
    ImFont* const font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float spacing = size * theme::menu_label_spacing_share;
    const char* const end = text + std::strlen(text);
    float width = 0.0f;
    for (const char* p = text; p < end;) {
        const int length = char_length(p, end);
        char glyph[8] = {};
        std::memcpy(glyph, p, static_cast<std::size_t>(std::min(length, 7)));
        if (length == 1 && glyph[0] >= 'a' && glyph[0] <= 'z')
            glyph[0] = static_cast<char>(glyph[0] - 'a' + 'A');
        const char* const glyph_end = glyph + std::min(length, 7);
        if (draw != nullptr)
            draw->AddText(font, size, ImVec2(at.x + width, at.y), colour, glyph,
                glyph_end);
        width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph, glyph_end).x
            + spacing;
        p += length;
    }
    return std::max(0.0f, width - spacing);
}

/** @brief Strokes a right-pointing chevron centred at @p centre. */
void draw_chevron(ImDrawList* draw, ImVec2 centre, ImU32 colour) {
    const float half = theme::menu_chevron_half_height;
    draw->PathLineTo(ImVec2(centre.x - half * 0.5f, centre.y - half));
    draw->PathLineTo(ImVec2(centre.x + half * 0.5f, centre.y));
    draw->PathLineTo(ImVec2(centre.x - half * 0.5f, centre.y + half));
    draw->PathStroke(colour, ImDrawFlags_None, theme::menu_chevron_weight);
}

[[nodiscard]] ImU32 u32(ImVec4 colour) {
    return ImGui::ColorConvertFloat4ToU32(colour);
}

/**
 * @brief Draws the caret on the current popup window's top edge, pointing up at
 * @p label_x.
 *
 * The tip is above the window, so it goes on the viewport's foreground list, not
 * the window's clipped one. The base sits one border width inside the panel so
 * its fill covers the outline across the base; the outline is stroked on the two
 * slanted sides only, and runs on into the panel's top edge.
 */
void draw_caret(float label_x) {
    const ImVec2 at = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float half = theme::menu_caret_width * 0.5f;
    const float centre = menu_caret_centre(at.x, at.x + size.x, label_x);
    const float base = at.y + ImGui::GetStyle().PopupBorderSize;
    const ImVec2 left(centre - half, base);
    const ImVec2 tip(centre, at.y - theme::menu_caret_height);
    const ImVec2 right(centre + half, base);
    ImDrawList* const draw = ImGui::GetForegroundDrawList(
        ImGui::GetWindowViewport());
    draw->AddTriangleFilled(left, tip, right, u32(theme::menu_surface()));
    draw->PathLineTo(left);
    draw->PathLineTo(tip);
    draw->PathLineTo(right);
    draw->PathStroke(u32(theme::menu_border()), ImDrawFlags_None,
        ImGui::GetStyle().PopupBorderSize);
}

}

float menu_caret_centre(float panel_min_x, float panel_max_x,
    float label_x) noexcept {
    const float reach = theme::menu_caret_margin + theme::menu_caret_width * 0.5f;
    const float low = panel_min_x + reach;
    const float high = panel_max_x - reach;
    if (low > high)
        return (panel_min_x + panel_max_x) * 0.5f;
    return std::clamp(label_x, low, high);
}

menu_popup::menu_popup(const char* id, float width,
    std::optional<float> caret_x, float height)
    : caret_x_(caret_x) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(theme::menu_padding, theme::menu_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(0.0f, theme::menu_row_gap));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding,
        theme::menu_row_rounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::menu_surface());
    ImGui::PushStyleColor(ImGuiCol_Border, theme::menu_border());
    ImGui::PushStyleColor(ImGuiCol_Header, theme::menu_selected_colour());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::menu_hover_colour());
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::menu_active_colour());
    if (width > 0.0f)
        ImGui::SetNextWindowSize(ImVec2(width, height));
    else
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(theme::menu_min_width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    open_ = ImGui::BeginPopup(id);
    if (open_)
        ImGui::PushFont(icon::mono_font());
}

menu_popup::~menu_popup() {
    if (open_) {
        ImGui::PopFont();
        if (caret_x_ && ImGui::GetWindowViewport() == ImGui::GetMainViewport())
            draw_caret(*caret_x_);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(style_colours_pushed);
    ImGui::PopStyleVar(style_vars_pushed);
}

bool menu_item_row(const menu_row& row) {
    const float height = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    const float width = ImGui::GetContentRegionAvail().x;

    /* A hovered chosen row stays the chosen purple, as the reference's does. */
    if (row.selected)
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            theme::menu_selected_colour());
    ImGui::PushID(row.label);
    const bool pressed = ImGui::Selectable("##row", row.selected,
        row.enabled ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled,
        ImVec2(width, height));
    ImGui::PopID();
    if (row.selected)
        ImGui::PopStyleColor();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const bool lit = row.enabled && (row.selected || ImGui::IsItemHovered());
    const ImU32 disabled = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 label_colour = row.enabled ? ImGui::GetColorU32(ImGuiCol_Text)
                                           : disabled;
    /* The icon and the chevron are quiet until the row is lit. */
    const ImU32 glyph_colour = !row.enabled ? disabled
        : lit ? label_colour
              : u32(theme::dim_text());
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const float top = min.y + theme::menu_row_padding_y;
    float left = min.x + theme::menu_row_padding_x;
    if (row.icon != nullptr) {
        draw->AddText(ImVec2(left, top), glyph_colour, row.icon);
        left += ImGui::GetFontSize() * theme::menu_icon_share;
    } else if (row.checkable) {
        /*
         * The check column. It is reserved whether or not the row draws a
         * mark, so a menu of switches keeps its labels on one line and a
         * switch reads as a tick rather than as a chosen row.
         */
        if (row.checked)
            draw->AddText(ImVec2(left, top), glyph_colour, icon::ready);
        left += ImGui::GetFontSize() * theme::menu_icon_share;
    }
    draw->AddText(ImVec2(left, top), label_colour, row.label);

    float right = max.x - theme::menu_row_padding_x;
    if (row.submenu) {
        draw_chevron(draw,
            ImVec2(right - theme::menu_chevron_half_height * 0.5f,
                (min.y + max.y) * 0.5f),
            glyph_colour);
        right -= theme::menu_chevron_half_height + theme::menu_shortcut_gap * 0.5f;
    }
    if (row.shortcut != nullptr) {
        const float shortcut_width = ImGui::CalcTextSize(row.shortcut).x;
        draw->AddText(ImVec2(right - shortcut_width, top),
            row.enabled ? u32(theme::dim_text()) : disabled, row.shortcut);
    }
    return pressed;
}

void menu_separator() {
    ImGui::PushStyleColor(ImGuiCol_Separator, theme::menu_border());
    ImGui::Separator();
    ImGui::PopStyleColor();
}

void menu_heading(const char* text) {
    const float height = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, height));
    const ImVec2 min = ImGui::GetItemRectMin();
    draw_upper(ImGui::GetWindowDrawList(),
        ImVec2(min.x + theme::menu_row_padding_x,
            min.y + theme::menu_row_padding_y),
        text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
}

bool menu_label(const char* text, bool open, float height) {
    ImGui::PushFont(icon::mono_font());
    const float width = draw_upper(nullptr, ImVec2(0.0f, 0.0f), text, 0)
        + 2.0f * theme::menu_label_padding_x;
    ImGui::PushID(text);
    const bool pressed = ImGui::InvisibleButton("##label",
        ImVec2(width, height > 0.0f ? height : ImGui::GetFrameHeight()));
    ImGui::PopID();
    const ImVec2 min = ImGui::GetItemRectMin();
    const bool lit = open || ImGui::IsItemHovered();
    draw_upper(ImGui::GetWindowDrawList(),
        ImVec2(min.x + theme::menu_label_padding_x,
            min.y + (ImGui::GetItemRectSize().y - ImGui::GetFontSize()) * 0.5f),
        text,
        lit ? ImGui::GetColorU32(ImGuiCol_Text) : u32(theme::dim_text()));
    ImGui::PopFont();
    return pressed;
}

}
