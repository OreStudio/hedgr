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
#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::state::window_handle;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/**
 * @brief The rows the grid is pretending to page through.
 *
 * The runtime owns the real page; until it delivers one, the grid draws a
 * fixed page so the filter, the paging controls and the row count are all
 * visibly working rather than merely present.
 */
constexpr std::size_t fake_rows_per_page = 40;

std::size_t page_count(std::uint32_t page_size) {
    if (page_size == 0)
        return 1;
    const std::size_t total = fake_rows_per_page * 5;
    return (total + page_size - 1) / page_size;
}

bool filter_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    return std::search(haystack.begin(), haystack.end(),
               needle.begin(), needle.end(),
               [&](char a, char b) { return lower(a) == lower(b); })
        != haystack.end();
}

}

namespace hedgr::ui::render {

/**
 * @brief A grid window: a paged table of one resource.
 *
 * The page controls are icons, like everything else, and the row count is
 * phrased in terms of what the user can see rather than what the store holds:
 * "rows 1-40 of 200" answers the question a person is actually asking.
 */
void render_grid(window_handle handle, const render_input& input,
    window_ui_state& state) {
    using hedgr::model::domain::grid_state;

    const auto& grid = std::get<grid_state>(input.windows.state(handle));

    /*
     * The filter box is prefixed with the search icon drawn beside it rather
     * than inside it: an icon inside a frame would need the frame's own
     * layout, and the alphabet of this interface keeps icons and text apart.
     */
    ImGui::TextUnformatted(hedgr::ui::icon::search);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##grid-filter", "filter rows",
        state.filter.data(), state.filter.size());

    const std::uint32_t page_size = grid.page_size == 0 ? 40u : grid.page_size;
    const std::size_t pages = page_count(page_size);
    const std::size_t page = state.selected_row < 0
        ? 0
        : static_cast<std::size_t>(state.selected_row);
    const std::size_t current = page < pages ? page : 0;

    const auto step = [&](int direction) {
        const auto next = static_cast<long>(current) + direction;
        if (next < 0)
            state.selected_row = 0;
        else if (next >= static_cast<long>(pages))
            state.selected_row = static_cast<int>(pages) - 1;
        else
            state.selected_row = static_cast<int>(next);
    };

    const bool on_first = current == 0;
    const bool on_last = current + 1 >= pages;

    /*
     * Paging is four icon buttons rather than a slider: a slider cannot say
     * how many pages there are, and the count is already written beside them.
     */
    ImGui::BeginDisabled(on_first);
    if (ImGui::Button(hedgr::ui::icon::page_first))
        state.selected_row = 0;
    ImGui::SetItemTooltip("First page");
    ImGui::SameLine();
    if (ImGui::Button(hedgr::ui::icon::page_previous))
        step(-1);
    ImGui::SetItemTooltip("Previous page");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(on_last);
    if (ImGui::Button(hedgr::ui::icon::page_next))
        step(1);
    ImGui::SetItemTooltip("Next page");
    ImGui::SameLine();
    if (ImGui::Button(hedgr::ui::icon::page_last))
        state.selected_row = static_cast<int>(pages) - 1;
    ImGui::SetItemTooltip("Last page");
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool refresh_clicked = ImGui::Button(hedgr::ui::icon::refresh);
    ImGui::SetItemTooltip("Refresh now");
    if (refresh_clicked) {
        command request;
        request.kind = command_kind::refresh_window;
        request.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(request));
    }

    ImGui::SameLine();
    ImGui::Text("page %zu of %zu", current + 1, pages);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("##grid", 3, flags, ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(grid.resource.empty() ? "resource" : grid.resource.c_str(),
            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("tenant", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto first = current * page_size;
        std::size_t shown = 0;
        for (std::size_t offset = 0; offset < page_size; ++offset) {
            const auto row_number = first + offset;
            const std::string label = std::format("{}-{}", grid.resource,
                row_number);
            if (!filter_contains(label, state.filter.data()))
                continue;

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(row_number));
            ImGui::TableNextColumn();
            ImGui::Text("%zu", row_number + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(grid.tenant_id.empty()
                    ? "(session default)"
                    : grid.tenant_id.c_str());
            ImGui::PopID();
            ++shown;
        }

        ImGui::EndTable();

        ImGui::Text("%zu of %zu rows shown", shown,
            static_cast<std::size_t>(page_size));
    }
}

}
