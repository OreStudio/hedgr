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
#include "hedgr.ui/render/instances_panel.hpp"

#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;

/**
 * @brief The live instance with this id, or null.
 *
 * A walk of the rows rather than the table's find, which builds a string for
 * its map and so allocates for an id past the small-string size. The walk is
 * over a handful of pinned rows against the instance rows, once per frame.
 */
[[nodiscard]] const domain::instance* instance_by_id(const render_input& input,
    std::string_view id) {
    const auto& layout = input.instances.layout();
    const auto rows = input.instances.rows();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (layout.is_alive(index) && rows[index].id == id)
            return &rows[index];
    }
    return nullptr;
}

/** @brief The probe's state for an instance, or the unobserved one. */
struct tile_status final {
    ImU32 colour = 0;
    const char* word = "";
};

/** @brief The status of the instance in slab slot @p index, read by handle. */
[[nodiscard]] tile_status status_of(const render_input& input,
    std::uint32_t index) {
    domain::probe_result none;
    const hedgr::model::state::instance_handle handle{index,
        input.instances.layout().generation[index]};
    const auto* probe = input.probes.get(handle);
    const domain::probe_result& result = probe != nullptr ? *probe : none;
    return {theme::probe_state_colour(result, probe != nullptr),
        theme::probe_state_word(result, probe != nullptr)};
}

/**
 * @brief Draws the photo at @p min, @p side square, or the instance glyph on
 * the theme's surface colour when there is no picture.
 */
void draw_photo(ImDrawList* draw, ImVec2 min, float side, std::uint64_t texture,
    const char* glyph) {
    const ImVec2 max(min.x + side, min.y + side);
    const float rounding = ImGui::GetStyle().FrameRounding;
    if (texture != 0) {
        draw->AddImageRounded(static_cast<ImTextureID>(texture), min, max,
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, rounding);
        return;
    }
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg),
        rounding);
    const ImVec2 size = ImGui::CalcTextSize(glyph);
    draw->AddText(ImVec2(min.x + (side - size.x) * 0.5f,
                       min.y + (side - size.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
}

/** @brief Draws @p text at @p at, cut off at @p right. */
void draw_clipped_text(ImDrawList* draw, ImVec2 at, float right,
    std::string_view text, ImU32 colour) {
    draw->PushClipRect(ImVec2(at.x, at.y - 1.0f),
        ImVec2(right, at.y + ImGui::GetTextLineHeight() + 1.0f), true);
    draw->AddText(at, colour, text.data(), text.data() + text.size());
    draw->PopClipRect();
}

/**
 * @brief What hovering a shortcut says the click will do. The old bar's words.
 *
 * A kind the interface cannot open says so, so a click that does nothing is
 * explained before it is made.
 */
[[nodiscard]] std::string shortcut_tooltip(const render_input& input,
    const domain::shortcut& row) {
    switch (row.target_kind) {
    case domain::window_kind::instance_manager:
        return "Bring the Instances window to the front";
    case domain::window_kind::accounts: {
        const auto* instance = instance_by_id(input, row.target_id);
        if (instance == nullptr)
            return "The instance this shortcut named is no longer in the store";
        return std::format("Open the accounts of {}", instance->name);
    }
    default:
        break;
    }
    return std::format("A {} shortcut. This kind cannot be opened yet",
        domain::to_string(row.target_kind));
}

/**
 * @brief Does what a shortcut means, as the removed bar did, for the kinds that
 * mean something yet. Returns whether it did anything.
 *
 * An inert kind raises no signal, because a sound would claim that something
 * happened.
 */
bool activate_shortcut(const render_input& input, const domain::shortcut& row,
    menu_bar_result& result) {
    switch (row.target_kind) {
    case domain::window_kind::instance_manager:
        request_instances_window(input);
        result.focus_instances = true;
        break;
    case domain::window_kind::accounts:
        request_accounts_window(input, row.target_id);
        break;
    default:
        return false;
    }
    raise_shortcut_activated(input, shortcut_tooltip(input, row));
    return true;
}

/** @brief The pinned row: one small tile per catalogue shortcut, wrapping. */
bool draw_pinned(const render_input& input, menu_bar_result& result) {
    const auto& rows = input.shortcuts.rows();
    if (rows.empty())
        return false;
    const float pad = theme::instances_tile_pad;
    const float gap = theme::instances_tile_gap;
    const float photo = theme::instances_pin_photo;
    const float height = photo + 2.0f * pad;
    const float room = ImGui::GetContentRegionAvail().x;
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* const draw = ImGui::GetWindowDrawList();

    bool picked = false;
    float x = 0.0f;
    float y = 0.0f;
    for (const auto& row : rows) {
        const auto* instance = row.target_kind == domain::window_kind::accounts
            ? instance_by_id(input, row.target_id) : nullptr;
        std::string_view name = domain::to_string(row.target_kind);
        if (row.target_kind == domain::window_kind::instance_manager)
            name = "Instances";
        else if (row.target_kind == domain::window_kind::accounts)
            name = instance != nullptr ? std::string_view{instance->name}
                                       : std::string_view{row.target_id};
        const float text = ImGui::CalcTextSize(name.data(),
            name.data() + name.size()).x;
        const float width = pad + photo + pad + text + pad;
        if (x > 0.0f && x + width > room) {
            x = 0.0f;
            y += height + gap;
        }
        const ImVec2 min(base.x + x, base.y + y);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(row.id.c_str());
        const bool clicked = ImGui::Selectable("##pin", false,
            ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(width, height));
        ImGui::PopID();

        const std::uint64_t texture = instance != nullptr
                && input.instance_icons != nullptr
            ? input.instance_icons->find(instance->icon_key) : 0;
        draw_photo(draw, ImVec2(min.x + pad, min.y + pad), photo, texture,
            theme::window_kind_icon(row.target_kind));
        draw_clipped_text(draw,
            ImVec2(min.x + 2.0f * pad + photo,
                min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
            min.x + width - pad, name, ImGui::GetColorU32(ImGuiCol_Text));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", shortcut_tooltip(input, row).c_str());
        if (clicked && activate_shortcut(input, row, result))
            picked = true;
        x += width + gap;
    }
    ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + y + height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return picked;
}

/** @brief Brings the filtered list up to date with the text and the table. */
void refresh_matches(const render_input& input,
    widget::instances_panel_state& state) {
    const auto& table = input.instances;
    if (state.built && state.version == table.version()
        && state.applied.view() == state.filter.view())
        return;
    state.matches.clear();
    state.total = 0;
    const auto& layout = table.layout();
    const auto rows = table.rows();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        ++state.total;
        if (widget::instance_matches(state.filter.view(), rows[index].name,
                rows[index].id))
            state.matches.push_back(index);
    }
    std::memcpy(state.applied.text, state.filter.text,
        sizeof state.applied.text);
    state.version = table.version();
    state.built = true;
}

/** @brief The filter box and, right-aligned beside it, shown of total. */
void draw_filter(widget::instances_panel_state& state, bool focus) {
    char count[32];
    std::snprintf(count, sizeof count, "%zu/%zu", state.matches.size(),
        state.total);
    const float room = ImGui::GetContentRegionAvail().x;
    const float gap = theme::instances_tile_gap;
    ImGui::SetNextItemWidth(
        std::max(1.0f, room - ImGui::CalcTextSize(count).x - gap));
    if (focus)
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##instances-filter", "Filter instances",
        state.filter.text, sizeof state.filter.text);
    ImGui::SameLine(0.0f, gap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", count);
}

/** @brief The tooltip of an instance tile: name, id and address, status. */
void draw_instance_tooltip(const domain::instance& row,
    const tile_status& status) {
    if (!ImGui::BeginTooltip())
        return;
    ImGui::TextUnformatted(row.name.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
    ImGui::TextUnformatted(row.id.c_str());
    if (!row.nats_url.empty())
        ImGui::TextUnformatted(row.nats_url.c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(
        status.colour));
    ImGui::TextUnformatted(status.word);
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

/** @brief The empty body: the no data mark, with the words in its tooltip. */
void draw_empty(std::size_t total) {
    widget::view_facts facts;
    facts.session = domain::session_status::authenticated;
    facts.has_data = false;
    facts.empty_what = total == 0 ? "There are no instances in the store."
                                  : "No instance matches the filter.";
    widget::draw_view_state_inline(widget::view_state_of(facts),
        ImGui::GetContentRegionAvail());
}

/** @brief The scrolling grid. Returns whether an instance was picked. */
bool draw_grid(const render_input& input, menu_bar_result& result,
    const widget::instances_panel_state& state) {
    const float photo = theme::instances_tile_photo;
    const float pad = theme::instances_tile_pad;
    const float gap = theme::instances_tile_gap;
    const float width = photo + 2.0f * pad;
    const float line = ImGui::GetTextLineHeight();
    const float height = pad + photo + pad + line + pad;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool picked = false;
    if (ImGui::BeginChild("##instances-grid",
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        if (state.matches.empty()) {
            draw_empty(state.total);
        } else {
            const float room = ImGui::GetContentRegionAvail().x
                - ImGui::GetStyle().ScrollbarSize;
            const int columns = widget::tile_columns(room, width, gap);
            const int count = static_cast<int>(state.matches.size());
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float pitch = height + gap;
            const int first = std::max(0,
                static_cast<int>(std::floor(ImGui::GetScrollY() / pitch)));
            const int last = static_cast<int>(std::floor(
                (ImGui::GetScrollY() + ImGui::GetWindowHeight()) / pitch));
            const int begin = std::min(count, first * columns);
            const int end = std::min(count, (last + 1) * columns);
            const auto rows = input.instances.rows();
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            for (int i = begin; i < end; ++i) {
                const std::uint32_t slot = state.matches[static_cast<std::size_t>(i)];
                const auto& row = rows[slot];
                const auto at = widget::tile_position(i, columns, width, height,
                    gap);
                const ImVec2 min(origin.x + at.x, origin.y + at.y);
                ImGui::SetCursorScreenPos(min);
                ImGui::PushID(row.id.c_str());
                const bool clicked = ImGui::Selectable("##tile", false,
                    ImGuiSelectableFlags_NoAutoClosePopups,
                    ImVec2(width, height));
                ImGui::PopID();

                const std::uint64_t texture = input.instance_icons != nullptr
                    ? input.instance_icons->find(row.icon_key) : 0;
                draw_photo(draw, ImVec2(min.x + pad, min.y + pad), photo,
                    texture, icon::instance);
                const auto status = status_of(input, slot);
                const float dot = theme::instances_status_dot;
                const float text_y = min.y + pad + photo + pad;
                draw->AddCircleFilled(
                    ImVec2(min.x + pad + dot, text_y + line * 0.5f), dot,
                    status.colour);
                draw_clipped_text(draw,
                    ImVec2(min.x + pad + 2.0f * dot + pad * 0.5f, text_y),
                    min.x + width - pad, row.name,
                    ImGui::GetColorU32(ImGuiCol_Text));
                if (ImGui::IsItemHovered())
                    draw_instance_tooltip(row, status);
                if (clicked) {
                    request_instances_window(input);
                    result.focus_instances = true;
                    raise_shortcut_activated(input,
                        "Open the Instances window on " + row.name);
                    picked = true;
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x,
                origin.y + widget::tile_grid_height(count, columns, height, gap)));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return picked;
}

}

void draw_instances_panel(const render_input& input,
    widget::instances_panel_state& state, menu_bar_result& result) {
    const bool opening = !state.shown;
    state.shown = true;
    if (opening) {
        state.filter.text[0] = '\0';
        state.built = false;
    }
    refresh_matches(input, state);

    bool picked = draw_pinned(input, result);
    if (!input.shortcuts.rows().empty())
        widget::menu_separator();
    draw_filter(state, opening);
    ImGui::Dummy(ImVec2(0.0f, theme::instances_tile_pad));
    picked = draw_grid(input, result, state) || picked;

    if (picked && !ImGui::GetIO().KeyCtrl)
        ImGui::CloseCurrentPopup();
}

}
