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
#include "hedgr.ui/widget/cards.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include <algorithm>

namespace hedgr::ui::widget {

namespace theme = hedgr::ui::theme;

int card_columns(float pane_width) {
    const int columns = static_cast<int>(
        (pane_width + card_gap) / (card_min_width + card_gap));
    return std::clamp(columns, 1, 4);
}

float card_width(float pane_width, int columns) {
    return (pane_width - card_gap * static_cast<float>(columns - 1))
        / static_cast<float>(columns);
}

float card_height() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return card_padding * 2.0f + ImGui::GetTextLineHeight() * 2.0f
        + ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
}

void card_slot(int& column, int columns) {
    if (column > 0)
        ImGui::SameLine(0.0f, card_gap);
    column = (column + 1) % columns;
}

bool begin_card(const char* id, float width, float height, bool marked) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::card_surface());
    ImGui::PushStyleColor(ImGuiCol_Border,
        marked ? ImGui::ColorConvertU32ToFloat4(theme::edit_changed_colour)
               : theme::card_border());

    const theme::background_mark parent = theme::mark_background();
    const bool open = ImGui::BeginChild(id, ImVec2(width, height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (open)
        theme::shade_window_background(theme::card_gradient, parent);

    if (marked) {
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float bottom = origin.y + ImGui::GetWindowHeight();
        draw->AddRectFilled(ImVec2(origin.x + 2.0f, origin.y + 6.0f),
            ImVec2(origin.x + 4.0f, bottom - 6.0f),
            theme::edit_changed_colour, 1.0f);
    }
    return open;
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

bool begin_section_card(const char* id, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::card_surface());
    ImGui::PushStyleColor(ImGuiCol_Border, theme::card_border());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(section_card_padding, section_card_padding));
    const theme::background_mark parent = theme::mark_background();
    const bool open = ImGui::BeginChild(id, ImVec2(width, 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY
            | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (open)
        theme::shade_window_background(theme::card_gradient, parent);
    /*
     * The child has read its fill, outline and padding by now, and popping them
     * here keeps them off any child the card's body opens.
     */
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return open;
}

void section_card_divider() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(origin,
        ImVec2(origin.x + width, origin.y + 1.0f),
        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(width, 1.0f));
}

void end_section_card() {
    ImGui::EndChild();
}

void card_title(const char* glyph, std::string_view title, ImU32 colour) {
    const ImVec4 as_float = ImGui::ColorConvertU32ToFloat4(colour);
    if (glyph != nullptr) {
        /*
         * The glyph is the connected green whatever the title's words are,
         * changed field or not, because the reference draws every card title's
         * icon in it and the mark of an unsaved edit is already carried by the
         * card's outline, its bar, its words and its value.
         */
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(theme::card_title_icon_colour()));
        ImGui::TextUnformatted(glyph);
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, as_float);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopStyleColor();
}

void draw_card(const card_spec& card, float width, float height) {
    /*
     * A wrapping value is measured before the card is opened, because a child is
     * given its height when it is created and a taller box cannot be asked for
     * afterwards. The width the text wraps at is the card's interior less the
     * bar the input keeps, which is the width the input itself will use once the
     * card is open.
     */
    const ImGuiStyle& style = ImGui::GetStyle();
    const float interior = width - style.WindowPadding.x * 2.0f;
    const float wrap_width = std::max(interior - style.ScrollbarSize, 1.0f);
    const bool wrapped = card.wrap && !card.value.empty();
    const float draw_height = wrapped
        ? height - ImGui::GetFrameHeight()
            + text_value_height(card.value, wrap_width)
        : height;

    const bool open = begin_card(card.id, width, draw_height, false);
    if (open) {
        card_title(
            card.glyph, card.title, ImGui::GetColorU32(theme::dim_text()));

        const ImU32 colour =
            card.colour != 0 ? card.colour : ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(colour));
        if (wrapped)
            text_value(card.id, card.value, wrap_width);
        else
            text_line(card.id, card.value, -FLT_MIN);
        ImGui::PopStyleColor();

        if (!card.caption.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(card.caption.c_str());
            ImGui::PopStyleColor();
        }
    }
    end_card();
}

void draw_topics(const std::vector<topic_spec>& topics, float pane_width,
    float width, float height, int columns) {
    int column = 0;
    for (const auto& topic : topics) {
        if (topic.cards.empty())
            continue;
        if (topic.cards.size() > 1) {
            ImGui::SeparatorText(topic.title);
            column = 0;
        }
        for (const auto& card : topic.cards) {
            if (card.full_width) {
                column = 0;
                draw_card(card, pane_width, height);
                continue;
            }
            card_slot(column, columns);
            draw_card(card, width, height);
        }
    }
}

}
