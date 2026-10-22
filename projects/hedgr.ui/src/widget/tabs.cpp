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
#include "hedgr.ui/widget/tabs.hpp"

#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <format>

namespace hedgr::ui::widget {

namespace {

/*
 * Fractions of the current font size, so a larger face gets a larger badge
 * rather than a number spilling out of a fixed capsule.
 */
constexpr float badge_scale = 0.75f;
constexpr float badge_padding_x_ratio = 0.45f;
constexpr float badge_padding_y_ratio = 0.2f;
constexpr float chip_rounding = 3.0f;

/** @brief A count's digits, held on the stack so a frame allocates nothing. */
struct count_digits final {
    std::array<char, 16> buffer{};
    std::size_t size = 0;

    [[nodiscard]] std::string_view view() const {
        return {buffer.data(), size};
    }
};

[[nodiscard]] count_digits count_text(int count) {
    count_digits result;
    const auto written = std::format_to_n(result.buffer.data(),
        static_cast<std::ptrdiff_t>(result.buffer.size()), "{}", count);
    result.size = static_cast<std::size_t>(written.out - result.buffer.data());
    return result;
}

[[nodiscard]] float badge_font_size() {
    return ImGui::GetFontSize() * badge_scale;
}

[[nodiscard]] float text_width(std::string_view text) {
    return hedgr::ui::icon::bold_font()->CalcTextSizeA(badge_font_size(),
        FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
}

/** @brief The badge's own size; a count is never narrower than it is tall. */
[[nodiscard]] ImVec2 badge_size(std::string_view text, bool capsule) {
    const float font_size = badge_font_size();
    const float height =
        std::ceil(font_size * (1.0f + 2.0f * badge_padding_y_ratio));
    const float padded = std::ceil(text_width(text)
        + 2.0f * font_size * badge_padding_x_ratio);
    return ImVec2(capsule ? std::max(padded, height) : padded, height);
}

[[nodiscard]] float badge_gap() {
    return ImGui::GetStyle().ItemInnerSpacing.x;
}

/** @brief Where a badge of @p size goes after @p label in the last tab. */
[[nodiscard]] ImRect badge_rect(const char* label, ImVec2 size) {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float left = min.x + ImGui::GetStyle().FramePadding.x
        + ImGui::CalcTextSize(label, nullptr, true).x + badge_gap();
    const float top = min.y + std::floor((max.y - min.y - size.y) * 0.5f);
    return ImRect(ImFloor(ImVec2(left, top)),
        ImFloor(ImVec2(left + size.x, top + size.y)));
}

void draw_badge(const char* label, std::string_view text, bool capsule,
    ImU32 fill, ImU32 border, ImU32 colour) {
    const ImVec2 size = badge_size(text, capsule);
    const ImRect box = badge_rect(label, size);
    const float rounding = capsule ? size.y * 0.5f : chip_rounding;
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
    if (fill != 0)
        draw->AddRectFilled(box.Min, box.Max, fill, rounding);
    if (border != 0)
        draw->AddRect(box.Min, box.Max, border, rounding, 0, 1.0f);
    const float font_size = badge_font_size();
    draw->AddText(hedgr::ui::icon::bold_font(), font_size,
        ImFloor(ImVec2(box.Min.x + (size.x - text_width(text)) * 0.5f,
            box.Min.y + (size.y - font_size) * 0.5f)),
        colour, text.data(), text.data() + text.size());
    draw->PopClipRect();
}

}

void underline_selected_tab() {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x, max.y - tab_underline_thickness), max,
        ImGui::GetColorU32(ImGuiCol_TabSelectedOverline));
}

float tab_badge_count_width(int count) {
    return badge_gap() + badge_size(count_text(count).view(), true).x;
}

float tab_badge_label_width(std::string_view text) {
    return badge_gap() + badge_size(text, false).x;
}

void reserve_tab_badge(const char* label, bool closable, float badge_width) {
    ImGui::SetNextItemWidth(
        ImGui::TabItemCalcSize(label, closable).x + badge_width);
}

void tab_badge_count(const char* label, int count) {
    draw_badge(label, count_text(count).view(), true,
        ImGui::GetColorU32(ImGuiCol_TabSelectedOverline), 0,
        ImGui::ColorConvertFloat4ToU32(theme::base_surface()));
}

void tab_badge_label(const char* label, std::string_view text) {
    draw_badge(label, text, false, 0,
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        ImGui::GetColorU32(theme::dim_text()));
}

}
