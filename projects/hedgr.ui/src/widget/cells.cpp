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
#include "hedgr.ui/widget/cells.hpp"

#include "hedgr.ui/icon/fonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

namespace hedgr::ui::widget {

namespace {

namespace theme = hedgr::ui::theme;

/*
 * Every size is a fraction of the current font size, so a desk with a larger
 * face gets larger cells rather than text spilling out of fixed boxes.
 */
constexpr float caption_scale = 0.75f;
constexpr float tracking_ratio = 0.12f;
constexpr float help_gap_ratio = 0.5f;
constexpr float help_diameter_ratio = 1.1f;
constexpr float pill_scale = 0.85f;
constexpr float pill_padding_x_ratio = 0.6f;
constexpr float pill_padding_y_ratio = 0.3f;
constexpr float cell_gap_ratio = 0.35f;
constexpr float hero_scale = 1.5f;
constexpr float hero_padding_x_ratio = 0.9f;
constexpr float hero_padding_y_ratio = 0.55f;
constexpr float hero_gap_ratio = 0.6f;
constexpr float hero_rounding = 6.0f;

/*
 * Heights are whole pixels so that edges land on pixel boundaries: a cell
 * whose outline fell between two rows would blur and bleed into the cell below.
 */
[[nodiscard]] float cell_gap() {
    return std::round(ImGui::GetFontSize() * cell_gap_ratio);
}

/** @brief A font at a size, which is all a draw list needs to place text. */
struct face final {
    ImFont* font = nullptr;
    float size = 0.0f;
};

/**
 * @brief Hands drawn text to ImGui's log, as ImGui's own text does.
 *
 * These widgets draw straight into the draw list, which ImGui's log never
 * sees, so a window logged or captured as text would lose its values.
 */
void log_text(ImVec2 at, std::string_view text) {
    if (ImGui::GetCurrentContext()->LogEnabled)
        ImGui::LogRenderedText(&at, text.data(), text.data() + text.size());
}

[[nodiscard]] face bold_at(float scale) {
    return face{hedgr::ui::icon::bold_font(), ImGui::GetFontSize() * scale};
}

[[nodiscard]] float tracking(const face& f) {
    return f.size * tracking_ratio;
}

[[nodiscard]] float help_diameter(const face& f) {
    return f.size * help_diameter_ratio;
}

/** @brief The height of a caption row, which always leaves room for help. */
[[nodiscard]] float caption_row_height(const face& f) {
    return std::ceil(std::max(f.size, help_diameter(f)));
}

/** @brief Calls @p visit with each code point of @p text, in capitals. */
template<class Visit>
void for_each_capital(std::string_view text, Visit&& visit) {
    const char* cursor = text.data();
    const char* const end = cursor + text.size();
    while (cursor < end) {
        unsigned int code = 0;
        cursor += ImTextCharFromUtf8(&code, cursor, end);
        if (code >= 'a' && code <= 'z')
            code -= 'a' - 'A';
        visit(static_cast<ImWchar>(code));
    }
}

[[nodiscard]] float tracked_width(const face& f, std::string_view text) {
    ImFontBaked* const baked = f.font->GetFontBaked(f.size);
    float width = 0.0f;
    int count = 0;
    for_each_capital(text, [&](ImWchar code) {
        width += baked->GetCharAdvance(code);
        ++count;
    });
    if (count == 0)
        return 0.0f;
    return width + tracking(f) * static_cast<float>(count - 1);
}

[[nodiscard]] ImVec2 measure_caption(const face& f, std::string_view text,
    bool help) {
    float width = tracked_width(f, text);
    float height = f.size;
    if (help) {
        width += f.size * help_gap_ratio + help_diameter(f);
        height = caption_row_height(f);
    }
    return ImVec2(width, height);
}

/** @brief Draws a caption whose row of @p row_height starts at @p origin. */
void draw_caption_at(ImDrawList* draw, const face& f, ImVec2 origin,
    std::string_view text, bool help, float row_height, const ImVec4& clip) {
    ImFontBaked* const baked = f.font->GetFontBaked(f.size);
    const ImU32 colour = ImGui::GetColorU32(theme::dim_text());
    const float top = ImFloor(origin.y + (row_height - f.size) * 0.5f);
    const float step = tracking(f);
    log_text(ImVec2(origin.x, top), text);

    float x = origin.x;
    for_each_capital(text, [&](ImWchar code) {
        f.font->RenderChar(draw, f.size, ImVec2(ImFloor(x), top), colour, code,
            &clip);
        x += baked->GetCharAdvance(code) + step;
    });

    if (!help)
        return;
    const float diameter = help_diameter(f);
    const ImVec2 centre(
        origin.x + tracked_width(f, text) + f.size * help_gap_ratio
            + diameter * 0.5f,
        origin.y + row_height * 0.5f);
    const ImU32 quiet = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddCircle(centre, diameter * 0.5f - 0.5f, quiet, 0, 1.0f);
    const float mark = baked->GetCharAdvance('?');
    f.font->RenderChar(draw, f.size,
        ImVec2(ImFloor(centre.x - mark * 0.5f), ImFloor(centre.y - f.size * 0.5f)),
        colour, '?', &clip);
}

[[nodiscard]] float pill_padding_x(const face& f) {
    return f.size * pill_padding_x_ratio;
}

[[nodiscard]] float pill_padding_y(const face& f) {
    return f.size * pill_padding_y_ratio;
}

[[nodiscard]] float pill_height(const face& f) {
    return std::ceil(f.size + 2.0f * pill_padding_y(f));
}

[[nodiscard]] ImVec2 measure_pill(const face& f, std::string_view text) {
    const ImVec2 size = f.font->CalcTextSizeA(f.size, FLT_MAX, 0.0f,
        text.data(), text.data() + text.size());
    return ImVec2(size.x + 2.0f * pill_padding_x(f), pill_height(f));
}

/** @brief Draws a pill at @p origin, no wider than @p max_width. */
void draw_pill_at(ImDrawList* draw, const face& f, ImVec2 origin,
    std::string_view text, theme::tone tone, float max_width) {
    const ImVec2 size = measure_pill(f, text);
    const ImVec2 min = origin;
    const ImVec2 max(origin.x + std::min(size.x, max_width), origin.y + size.y);
    const theme::tone_colours colours = theme::pill_colours(tone);
    draw->AddRectFilled(min, max, colours.fill, value_pill_rounding);
    draw->AddRect(min, max, colours.border, value_pill_rounding, 0,
        value_pill_border);
    const ImVec4 clip(min.x, min.y, max.x, max.y);
    log_text(min, text);
    draw->AddText(f.font, f.size,
        ImVec2(min.x + pill_padding_x(f), min.y + pill_padding_y(f)),
        colours.text, text.data(), text.data() + text.size(), 0.0f, &clip);
}

[[nodiscard]] ImVec2 snapped_cursor() {
    return ImFloor(ImGui::GetCursorScreenPos());
}

/**
 * @brief Records an inline item of @p size in the middle of the line it joins,
 * and returns where to draw it, or nothing when it is clipped.
 *
 * A line that AlignTextToFramePadding or a taller item began is taller than a
 * caption or a pill. ImGui::Text centres itself on such a line, so a pill in a
 * row of text and buttons does the same rather than hanging from its top. On a
 * fresh line the offset is zero.
 */
[[nodiscard]] std::optional<ImVec2> place_inline(ImVec2 size) {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    const float top = std::max(0.0f,
        std::floor((window->DC.CurrLineSize.y - size.y) * 0.5f));
    const ImVec2 cursor = ImFloor(window->DC.CursorPos);
    const ImVec2 origin(cursor.x, cursor.y + top);
    ImGui::ItemSize(ImVec2(size.x, size.y + top));
    if (!ImGui::ItemAdd(
            ImRect(origin, ImVec2(origin.x + size.x, origin.y + size.y)), 0))
        return std::nullopt;
    return origin;
}

void help_tooltip(std::string_view help) {
    if (!help.empty())
        ImGui::SetItemTooltip("%.*s", static_cast<int>(help.size()),
            help.data());
}

/** @brief Lays out one cell and calls @p value with where its value goes. */
template<class Value>
void cell(std::string_view label, std::string_view help, float width,
    Value&& value) {
    const float height = labelled_value_height();
    const ImVec2 size(width, height);
    if (ImGui::IsRectVisible(size)) {
        const ImVec2 origin = snapped_cursor();
        const face caption_face = bold_at(caption_scale);
        const float row = caption_row_height(caption_face);
        const ImVec4 clip(origin.x, origin.y, origin.x + width,
            origin.y + height);
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        draw_caption_at(draw, caption_face, origin, label, !help.empty(), row,
            clip);
        value(draw, ImVec2(origin.x,
            origin.y + row + cell_gap()), clip);
    }
    ImGui::Dummy(size);
    help_tooltip(help);
}

}

float caption_tracking() {
    return tracking(bold_at(caption_scale));
}

ImVec2 caption_size(std::string_view text, std::string_view help) {
    return measure_caption(bold_at(caption_scale), text, !help.empty());
}

void caption(std::string_view text, std::string_view help) {
    const face f = bold_at(caption_scale);
    const ImVec2 size = measure_caption(f, text, !help.empty());
    if (const auto origin = place_inline(size)) {
        draw_caption_at(ImGui::GetWindowDrawList(), f, *origin, text,
            !help.empty(), size.y,
            ImVec4(origin->x, origin->y, origin->x + size.x,
                origin->y + size.y));
    }
    help_tooltip(help);
}

ImVec2 value_pill_size(std::string_view text) {
    return measure_pill(bold_at(pill_scale), text);
}

void value_pill(std::string_view text, theme::tone tone) {
    const face f = bold_at(pill_scale);
    const ImVec2 size = measure_pill(f, text);
    if (const auto origin = place_inline(size))
        draw_pill_at(ImGui::GetWindowDrawList(), f, *origin, text, tone,
            size.x);
}

float labelled_value_height() {
    return caption_row_height(bold_at(caption_scale))
        + cell_gap() + pill_height(bold_at(pill_scale));
}

void labelled_value(std::string_view label, std::string_view value,
    float width, std::string_view help) {
    cell(label, help, width,
        [&](ImDrawList* draw, ImVec2 origin, const ImVec4& clip) {
            const face f = bold_at(pill_scale);
            log_text(origin, value);
            draw->AddText(f.font, f.size,
                ImVec2(origin.x, origin.y + pill_padding_y(f)),
                ImGui::GetColorU32(ImGuiCol_Text), value.data(),
                value.data() + value.size(), 0.0f, &clip);
        });
}

void labelled_pill(std::string_view label, std::string_view value,
    theme::tone tone, float width, std::string_view help) {
    cell(label, help, width,
        [&](ImDrawList* draw, ImVec2 origin, const ImVec4&) {
            draw_pill_at(draw, bold_at(pill_scale), origin, value, tone, width);
        });
}

float hero_strip_height() {
    const float content = std::max({bold_at(hero_scale).size,
        pill_height(bold_at(pill_scale)),
        caption_row_height(bold_at(caption_scale))});
    return std::ceil(content
        + 2.0f * ImGui::GetFontSize() * hero_padding_y_ratio);
}

void hero_strip(std::string_view label, std::string_view value,
    theme::tone tone, std::string_view pill, std::string_view help) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = hero_strip_height();
    const ImVec2 size(width, height);
    if (ImGui::IsRectVisible(size)) {
        const ImVec2 origin = snapped_cursor();
        const ImVec2 corner(origin.x + width, origin.y + height);
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const theme::tone_colours colours = theme::band_colours(tone);
        theme::fill_gradient(draw, origin, corner, theme::band_fade(tone),
            theme::axis::horizontal, hero_rounding);
        draw->AddRect(origin, corner, colours.border, hero_rounding, 0, 1.0f);

        const float padding = ImGui::GetFontSize() * hero_padding_x_ratio;
        const ImVec4 clip(origin.x, origin.y, corner.x, corner.y);
        const face caption_face = bold_at(caption_scale);
        const float row = caption_row_height(caption_face);
        draw_caption_at(draw, caption_face,
            ImVec2(origin.x + padding, origin.y + (height - row) * 0.5f), label,
            !help.empty(), row, clip);

        float right = corner.x - padding;
        if (!pill.empty()) {
            const face pill_face = bold_at(pill_scale);
            const ImVec2 pill_size = measure_pill(pill_face, pill);
            right -= pill_size.x;
            draw_pill_at(draw, pill_face,
                ImFloor(ImVec2(right, origin.y + (height - pill_size.y) * 0.5f)),
                pill, tone, pill_size.x);
            right -= ImGui::GetFontSize() * hero_gap_ratio;
        }

        const face value_face = bold_at(hero_scale);
        const float value_width = value_face.font->CalcTextSizeA(
            value_face.size, FLT_MAX, 0.0f, value.data(),
            value.data() + value.size()).x;
        log_text(ImVec2(right - value_width, origin.y), value);
        draw->AddText(value_face.font, value_face.size,
            ImFloor(ImVec2(right - value_width,
                origin.y + (height - value_face.size) * 0.5f)),
            colours.text, value.data(), value.data() + value.size(), 0.0f,
            &clip);
    }
    ImGui::Dummy(size);
    help_tooltip(help);
}

}
