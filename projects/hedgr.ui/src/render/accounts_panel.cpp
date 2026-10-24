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
#include "hedgr.ui/render/accounts_panel.hpp"

#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string_view>
#include <utility>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;

/**
 * @brief An instance's name by key, falling back to the key itself.
 *
 * A walk of the slab rather than @c instance_table::find, which builds a string
 * for its map and so allocates for an id past the small-string size. This runs
 * once per account on every frame the panel is open.
 */
[[nodiscard]] std::string_view instance_name(const render_input& input,
    std::string_view id) {
    const auto& layout = input.instances.layout();
    const auto rows = input.instances.rows();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (layout.is_alive(index) && rows[index].id == id)
            return rows[index].name;
    }
    return id;
}

/**
 * @brief The tenant the account's last sign-in reached, or empty.
 *
 * A scan of the session slab rather than @c session_table::find, which joins
 * the pair into a string and so allocates on every lookup: this runs once per
 * account on every frame the panel is open. Sessions number one per account
 * that has signed in, so the scan is a handful of rows against a handful.
 */
[[nodiscard]] std::string_view tenant_of(const render_input& input,
    std::string_view instance_id, std::string_view account_id) {
    const auto& layout = input.sessions.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const hedgr::model::state::session_handle handle{index,
            layout.generation[index]};
        if (input.sessions.instance_id(handle) == instance_id
            && input.sessions.account_id(handle) == account_id)
            return input.sessions.tenant_name(handle);
    }
    return {};
}

/**
 * @brief Fills the panel's list from the tables, in the order it draws.
 *
 * Rebuilt every frame rather than cached against a table version: the tenant a
 * row is grouped under is the session table's last answer, and the session
 * table has no version to compare. The entries vector is kept between frames,
 * so a steady frame reuses its room instead of allocating a list.
 */
void rebuild(const render_input& input, widget::accounts_panel_state& state) {
    state.entries.clear();
    state.total = 0;
    const auto& layout = input.accounts.layout();
    const auto rows = input.accounts.rows();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const auto& row = rows[index];
        ++state.total;

        const std::string_view tenant =
            tenant_of(input, row.instance_id, row.id);
        const std::string_view name = row.name.empty()
            ? std::string_view{row.principal}
            : std::string_view{row.name};
        const std::string_view instance = instance_name(input, row.instance_id);
        if (!widget::account_matches(state.filter.view(), name, row.principal,
                instance, tenant))
            continue;
        state.entries.push_back(widget::account_entry{.slot = index,
            .tenant = tenant,
            .name = name,
            .principal = row.principal,
            .instance = instance});
    }
    std::sort(state.entries.begin(), state.entries.end(),
        widget::account_entry_less);
}

/** @brief The filter box and, right-aligned beside it, shown of total. */
void draw_filter(widget::accounts_panel_state& state, bool focus) {
    char count[32];
    std::snprintf(count, sizeof count, "%zu/%zu", state.entries.size(),
        state.total);
    const float gap = theme::instances_tile_gap;
    const float room = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(
        std::max(1.0f, room - ImGui::CalcTextSize(count).x - gap));
    if (focus)
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##accounts-filter", "Filter accounts",
        state.filter.text, sizeof state.filter.text);
    ImGui::SameLine(0.0f, gap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", count);
}

/** @brief The empty body: the no data mark, with the words in its tooltip. */
void draw_empty(std::size_t total) {
    widget::view_facts facts;
    facts.session = domain::session_status::authenticated;
    facts.has_data = false;
    facts.empty_what = total == 0 ? "There are no accounts in the store."
                                  : "No account matches the filter.";
    widget::draw_view_state_inline(widget::view_state_of(facts),
        ImGui::GetContentRegionAvail());
}

/** @brief Draws @p text at @p at, cut off at @p right. */
void draw_clipped_text(ImDrawList* draw, ImVec2 at, float right,
    std::string_view text, ImU32 colour) {
    draw->PushClipRect(ImVec2(at.x, at.y - 1.0f),
        ImVec2(right, at.y + ImGui::GetTextLineHeight() + 1.0f), true);
    draw->AddText(at, colour, text.data(), text.data() + text.size());
    draw->PopClipRect();
}

/** @brief The tooltip of an account row: name, principal, instance and tenant. */
void draw_account_tooltip(const widget::account_entry& entry) {
    if (!ImGui::BeginTooltip())
        return;
    ImGui::TextUnformatted(entry.name.data(),
        entry.name.data() + entry.name.size());
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(entry.principal.data(),
        entry.principal.data() + entry.principal.size());
    ImGui::TextUnformatted(entry.instance.data(),
        entry.instance.data() + entry.instance.size());
    const auto tenant = widget::tenant_heading(entry.tenant);
    ImGui::TextUnformatted(tenant.data(), tenant.data() + tenant.size());
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

/** @brief The scrolling list, under a heading per tenant. */
bool draw_list(const render_input& input, const widget::accounts_panel_state& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool picked = false;
    if (ImGui::BeginChild("##accounts-list",
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        if (state.entries.empty()) {
            draw_empty(state.total);
        } else {
            const auto rows = input.accounts.rows();
            const float pad = theme::menu_row_padding_x;
            const float gap = theme::instances_tile_gap;
            const float line = ImGui::GetTextLineHeight();
            const float height = line + 2.0f * theme::menu_row_padding_y;
            const float width = ImGui::GetContentRegionAvail().x;
            std::string_view previous;
            bool first = true;
            for (const auto& entry : state.entries) {
                if (first || entry.tenant != previous) {
                    const auto heading = widget::tenant_heading(entry.tenant);
                    char text[192];
                    std::snprintf(text, sizeof text, "%.*s",
                        static_cast<int>(heading.size()), heading.data());
                    widget::menu_heading(text);
                    previous = entry.tenant;
                    first = false;
                }

                const auto& row = rows[entry.slot];
                ImGui::PushID(row.id.c_str());
                const bool clicked = ImGui::Selectable("##account", false,
                    ImGuiSelectableFlags_None, ImVec2(width, height));
                ImGui::PopID();

                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const float top = min.y + theme::menu_row_padding_y;
                ImDrawList* const draw = ImGui::GetWindowDrawList();
                const float name_left = min.x + pad;
                draw->AddText(ImVec2(name_left, top),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    entry.name.data(), entry.name.data() + entry.name.size());

                const float principal_left = name_left
                    + ImGui::CalcTextSize(entry.name.data(),
                          entry.name.data() + entry.name.size()).x
                    + gap;
                const float instance_width = ImGui::CalcTextSize(
                    entry.instance.data(),
                    entry.instance.data() + entry.instance.size()).x;
                const float instance_left = max.x - pad - instance_width;
                const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                if (principal_left < instance_left) {
                    draw_clipped_text(draw, ImVec2(principal_left, top),
                        instance_left - gap, entry.principal, dim);
                    draw->AddText(ImVec2(instance_left, top), dim,
                        entry.instance.data(),
                        entry.instance.data() + entry.instance.size());
                }

                if (ImGui::IsItemHovered())
                    draw_account_tooltip(entry);
                if (clicked) {
                    request_accounts_window(input, row.instance_id);
                    request_account_selection(row.instance_id, row.id);
                    raise_shortcut_activated(input,
                        std::format("Open the accounts of {}", entry.instance));
                    picked = true;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return picked;
}

}

void draw_accounts_panel(const render_input& input,
    widget::accounts_panel_state& state) {
    const bool opening = !state.shown;
    state.shown = true;
    if (opening)
        state.filter.text[0] = '\0';
    rebuild(input, state);

    draw_filter(state, opening);
    ImGui::Dummy(ImVec2(0.0f, theme::instances_tile_pad));
    const bool picked = draw_list(input, state);
    if (picked && !ImGui::GetIO().KeyCtrl)
        ImGui::CloseCurrentPopup();
}

}
