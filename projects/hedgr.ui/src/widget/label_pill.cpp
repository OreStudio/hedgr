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
#include "hedgr.ui/widget/label_pill.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <format>

namespace hedgr::ui::widget {

namespace {

/**
 * @brief The padding as a fraction of the font size, and the rounding likewise.
 *
 * Fractions rather than pixels, because these are the two numbers that decide
 * whether the result reads as a pill: the padding has to be a visible fraction
 * of the text it surrounds and the rounding has to be at least half the height,
 * at whatever size the font happens to be.
 */
constexpr float padding_x_ratio = 0.45f;
constexpr float padding_y_ratio = 0.16f;
constexpr float gap_ratio = 0.5f;

/**
 * @brief The pill's text size, as a fraction of the body's.
 *
 * Small enough to read as a badge rather than as a sentence of the row's own
 * text, and large enough to read without leaning in at 1920 wide. Seven tenths
 * of the body size leaves the pill at about two thirds of a row's height, which
 * is what a badge is; the DPI and main font scale factors are applied on top,
 * so a 4K display scales the badge with everything else rather than leaving it
 * behind at a fixed pixel size.
 */
constexpr float pill_font_scale = 0.7f;

/** @brief A colour that stands for a whole set: the mean of their fills. */
[[nodiscard]] std::uint32_t mean_colour(const std::vector<label_pill>& pills,
    std::size_t first) {
    unsigned int red = 0;
    unsigned int green = 0;
    unsigned int blue = 0;
    for (std::size_t index = first; index < pills.size(); ++index) {
        const auto value = pills[index].background;
        red += (value >> 16) & 0xFFu;
        green += (value >> 8) & 0xFFu;
        blue += value & 0xFFu;
    }
    const auto count = static_cast<unsigned int>(pills.size() - first);
    const auto alpha = (pills[first].background >> 24) & 0xFFu;
    return (alpha << 24) | ((red / count) << 16) | ((green / count) << 8)
        | (blue / count);
}

}

pill_style pill_style_for_current_font() {
    const float height = ImGui::GetTextLineHeight();

    pill_style style;
    style.text_height = height;
    /*
     * Ratios rather than pixel floors. The floor that used to sit here kept the
     * padding visible at a large size and did the opposite at a small one: a
     * four-pixel minimum around a shrink-to-fit badge is a wide margin, and the
     * result is a box with small text in it rather than a smaller pill.
     */
    style.padding_x = height * padding_x_ratio;
    style.padding_y = height * padding_y_ratio;
    /*
     * The total distance between two pills, which is what ImGui will actually
     * leave: a SameLine advances by the style's ItemSpacing and then by the
     * extra offset it is given, so a gap counted here has to include that
     * spacing or the row is laid out wider than it was measured and the last
     * pill runs past the boundary it was supposed to respect.
     */
    style.gap = ImGui::GetStyle().ItemSpacing.x + std::max(2.0f, height * gap_ratio);
    /*
     * Half the pill's height, which is the largest radius that still fits: any
     * more is clamped by ImGui and reads as a rounded rectangle rather than a
     * pill, and any less leaves straight vertical sides.
     */
    style.rounding = (height + 2.0f * style.padding_y) * 0.5f;
    return style;
}

/**
 * @brief Binds the font the pills are drawn and measured in, at their size.
 *
 * One font at a smaller size rather than a second face in the atlas: ImGui
 * rasterises the size asked for, so the badge costs nothing at start-up. What
 * matters is that the measurement and the paint happen under the same size,
 * which is why this is pushed once around a whole row rather than per pill.
 */
void push_pill_font() {
    /*
     * FontSizeBase rather than GetFontSize(): the latter has the DPI and main
     * scale factors already applied, and passing it back would apply them a
     * second time.
     */
    ImGui::PushFont(nullptr,
        ImGui::GetStyle().FontSizeBase * pill_font_scale);
}

float pill_width(std::string_view text, const pill_style& style) noexcept {
    const float text_width =
        ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    return text_width + 2.0f * style.padding_x;
}

bool pill_fits(float width, float gap, float available) noexcept {
    return width + gap <= available;
}

std::size_t pills_that_fit(const std::vector<float>& widths, float gap,
    float available) noexcept {
    float used = 0.0f;
    std::size_t count = 0;
    for (const float width : widths) {
        const float advance = count == 0 ? width : width + gap;
        if (used + advance > available)
            break;
        used += advance;
        ++count;
    }
    return count;
}

std::vector<label_pill> pills_for(
    const std::vector<const hedgr::model::domain::label*>& labels) {
    std::vector<label_pill> pills;
    pills.reserve(labels.size());
    for (const auto* row : labels) {
        if (row == nullptr)
            continue;
        pills.push_back(label_pill{row->name,
            static_cast<std::uint32_t>(
                hedgr::ui::theme::label_colour(row->colour)),
            row->severity, row->id});
    }
    return pills;
}

void label_pill_draw(std::string_view text, std::uint32_t background,
    std::string_view tooltip) {
    const pill_style style = pill_style_for_current_font();
    const float width = pill_width(text, style);
    const float height = style.text_height + 2.0f * style.padding_y;

    /*
     * The active font and its size are captured rather than left to the
     * three-argument AddText, which resolves both from the draw list's owning
     * window: the pill is laid out by ImGui::CalcTextSize at the current scale,
     * and text painted at a different scale would spill out of the box the
     * layout reserved for it.
     */
    ImFont* const font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();

    const ImVec2 text_size =
        ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* const draw = ImGui::GetWindowDrawList();

    /*
     * The badge is centred in a row's worth of space rather than being given
     * the row's height: a pill as tall as the text it sits beside has stopped
     * being a badge. The row is claimed at the body's height below, so the
     * shorter pill does not shorten the rows around it.
     */
    const float row_height = ImGui::GetTextLineHeight();
    const float top = origin.y + std::max(0.0f, (row_height - height) * 0.5f);

    const ImVec2 top_left(origin.x, top);
    const ImVec2 bottom_right(origin.x + width, top + height);
    draw->AddRectFilled(top_left, bottom_right,
        static_cast<ImU32>(background), style.rounding,
        ImDrawFlags_RoundCornersAll);

    const ImVec2 text_position(
        origin.x + style.padding_x,
        top + (height - text_size.y) * 0.5f);
    draw->AddText(font, font_size, text_position,
        static_cast<ImU32>(hedgr::ui::theme::text_colour_on(background)),
        text.data(), text.data() + text.size());

    /*
     * The cursor is advanced by the pill's own width rather than by a text
     * item, because the pill is painted on the draw list rather than submitted
     * as a widget: what the layout has to know is that this many pixels of the
     * line are gone. The item claims the row's height while the pill inside it
     * stays short, so a badge smaller than the body text does not make the row
     * smaller with it. It is also what makes the tooltip -- the label's name
     * and its severity -- answer on hover.
     */
    ImGui::Dummy(ImVec2(width, row_height));
    if (!tooltip.empty())
        ImGui::SetItemTooltip("%.*s", static_cast<int>(tooltip.size()),
            tooltip.data());
}

float label_pill_row(const std::vector<label_pill>& pills, float available) {
    if (pills.empty())
        return 0.0f;

    /*
     * One push around the whole row, because the widths measured below and the
     * text drawn from them have to be the same size or the last pill runs past
     * the width the row was told it had.
     */
    push_pill_font();
    const pill_style style = pill_style_for_current_font();

    std::vector<float> widths;
    widths.reserve(pills.size());
    for (const auto& pill : pills)
        widths.push_back(pill_width(pill.text, style));

    /*
     * A non-positive width means the caller is not laying the row out in a
     * bounded space -- a form draws its pills under a full-width field -- so
     * every pill is drawn and none is counted away.
     */
    const bool bounded = available > 0.0f;
    std::size_t shown = pills.size();
    if (bounded)
        shown = pills_that_fit(widths, style.gap, available);

    float x = 0.0f;
    const auto draw_at = [&](std::string_view text, std::uint32_t background,
                             std::string_view tooltip) {
        if (x > 0.0f) {
            /*
             * No extra offset: the style's ItemSpacing is already inside
             * @c style.gap, and asking for it twice would draw the row wider
             * than the width it was measured against.
             */
            ImGui::SameLine(0.0f, 0.0f);
            x += style.gap;
        }
        label_pill_draw(text, background, tooltip);
        x += pill_width(text, style);
    };

    for (std::size_t index = 0; index < shown; ++index) {
        const auto& pill = pills[index];
        draw_at(pill.text, pill.background,
            std::format("{}  ({})", pill.text,
                hedgr::model::domain::to_string(pill.severity)));
    }

    if (shown < pills.size()) {
        /*
         * The count, and the names behind it in its tooltip. The tooltip is
         * where the omitted labels are allowed to be long, so the list stays
         * complete without the row having to give up more of its width.
         */
        std::string hidden;
        for (std::size_t index = shown; index < pills.size(); ++index) {
            if (!hidden.empty())
                hidden.append(", ");
            hidden.append(pills[index].text);
        }

        const std::string count = std::format("+{}", pills.size() - shown);
        const float width = pill_width(count, style);
        const float gap = shown == 0 ? 0.0f : style.gap;

        /*
         * The count too obeys the boundary. A cell too narrow for the count as
         * well as for a pill shows nothing rather than a pill written over the
         * column beside it, which is the one failure mode a row of labels must
         * not have.
         */
        if (!bounded || pill_fits(width, gap, available - x))
            draw_at(count, mean_colour(pills, shown), hidden);
    }

    ImGui::PopFont();
    return x;
}

}
