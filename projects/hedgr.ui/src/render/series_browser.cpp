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
#include "hedgr.ui/render/windows.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include <cctype>
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

[[nodiscard]] bool contains_ci(std::string_view haystack,
    std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

/*
 * The flags sit left of the name, on the row's own background, so a list of
 * pairs reads as a currency list before it reads as a word list.
 */
void draw_series_row(std::string_view label, float extra_indent) {
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_size = ImGui::GetItemRectSize();
    const auto draw = ImGui::GetWindowDrawList();
    const auto pair = hedgr::ui::widget::currencies_of(label);
    float x = rect_min.x + 4.0f + extra_indent;
    const float flag_y = rect_min.y + (rect_size.y - hedgr::ui::widget::flag_height) * 0.5f;
    if (!pair.base.empty()) {
        hedgr::ui::widget::draw_flag(draw, ImVec2(x, flag_y), pair.base);
        x += hedgr::ui::widget::flag_width + 2.0f;
    }
    if (!pair.quote.empty()) {
        hedgr::ui::widget::draw_flag(draw, ImVec2(x, flag_y), pair.quote);
        x += hedgr::ui::widget::flag_width + 4.0f;
    }
    if (x == rect_min.x + 4.0f + extra_indent)
        x += 4.0f;
    const float text_y =
        rect_min.y + (rect_size.y - ImGui::GetTextLineHeight()) * 0.5f;
    draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text),
        std::string{label}.c_str());
}

void open_chart(const render_input& input,
    const hedgr::model::state::session_handle session,
    const domain::market_series_row& row) {
    command request;
    request.kind = command_kind::open_window;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.new_window_kind = domain::window_kind::chart;
    request.series_id = row.id;
    request.series_label = row.label;
    input.commands.push(std::move(request));
}

}

namespace hedgr::ui::render {

void render_series_browser(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);

    ImGui::PushID("series-browser");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto session = input.windows.session_of(handle);
    if (!session
        || input.sessions.status(*session)
            != domain::session_status::authenticated) {
        ImGui::TextWrapped(
            "This window is not signed in. Sign in from the Accounts window "
            "to read the market series.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##filter", "filter by series, asset or URI",
        state.filter.data(), state.filter.size());
    ImGui::SameLine();
    if (ImGui::Button(icon::refresh))
        input.commands.push(command{
            .kind = command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    ImGui::SetItemTooltip("Read the series catalogue again");
    ImGui::SameLine();
    ImGui::Checkbox("Synthetic only", &state.market_synthetic_only);
    ImGui::SetItemTooltip("%s",
        "Show only series a synthetic feed produced");
    ImGui::SameLine();
    ImGui::Checkbox("My party only", &state.market_own_party_only);
    ImGui::SetItemTooltip("%s",
        "Show only the series the session's own party owns");
    const std::string instance_id{input.sessions.instance_id(*session)};
    const std::string account_id{input.sessions.account_id(*session)};
    const auto* catalogue =
        input.market.catalogue(instance_id, account_id);
    static const std::vector<domain::market_series_row> none;
    const auto& rows = catalogue != nullptr ? catalogue->series : none;

    ImGui::SameLine();
    ImGui::TextDisabled("%zu series", rows.size());

    const auto catalogue_state = catalogue != nullptr
        ? catalogue->state
        : hedgr::model::state::catalogue_state::unread;
    if (catalogue_state == hedgr::model::state::catalogue_state::failed) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", catalogue->error.c_str());
    } else if (catalogue_state
            == hedgr::model::state::catalogue_state::reading
        && rows.empty()) {
        ImGui::TextDisabled("Reading the market series...");
    } else if (catalogue_state == hedgr::model::state::catalogue_state::ready
        && rows.empty()) {
        ImGui::TextDisabled(
            "This deployment holds no market series for this session.");
    }

    const std::string session_party{input.sessions.party_id(*session)};
    const std::string filter{state.filter.data()};

    /*
     * A deployment keeps a series per party for the same instrument, and the
     * list is tenant-wide, so the same pair arrives once per party with the
     * same label. The session acts as one party, and that is the set it should
     * start on: five rows all reading "FX AUD/USD" are one instrument five
     * times, not five instruments. The box stays available for a reader who
     * wants the whole tenant.
     */
    if (!state.market_party_default_applied
        && catalogue_state == hedgr::model::state::catalogue_state::ready) {
        state.market_party_default_applied = true;
        state.market_own_party_only = !session_party.empty()
            && std::any_of(rows.begin(), rows.end(),
                [&session_party](const auto& row) {
                    return row.party_id == session_party;
                });
    }

    /*
     * The filtered series, in one vector so the combobox and the table list
     * exactly the same rows. The combobox is the quick way in; the table is the
     * overview, and a row in either opens the same chart.
     */
    std::vector<const domain::market_series_row*> visible;
    visible.reserve(rows.size());
    for (const auto& row : rows) {
        if (state.market_synthetic_only
            && row.producer_kind != "SYNTHETIC") {
            continue;
        }
        if (state.market_own_party_only && !session_party.empty()
            && row.party_id != session_party) {
            continue;
        }
        if (!contains_ci(row.label, filter)
            && !contains_ci(row.asset_class, filter)
            && !contains_ci(row.uri, filter)
            && !contains_ci(row.producer_kind, filter)) {
            continue;
        }
        visible.push_back(&row);
    }

    if (visible.empty() && !rows.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("No series match the filters.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear filters")) {
            state.market_synthetic_only = false;
            state.market_own_party_only = false;
            std::fill(state.filter.begin(), state.filter.end(), '\0');
        }
    }

    ImGui::TextUnformatted("Series");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(420.0f);
    const char* preview = state.market_selected_series_label.empty()
        ? "Select a series..."
        : state.market_selected_series_label.c_str();
    if (ImGui::BeginCombo("##series-combo", preview,
            ImGuiComboFlags_HeightLarge)) {
        for (const auto* row : visible) {
            ImGui::PushID(row->id.c_str());
            const bool chosen =
                row->id == state.market_selected_series_id;
            if (ImGui::Selectable("##combo-row", chosen)) {
                state.market_selected_series_id = row->id;
                state.market_selected_series_label = row->label;
                open_chart(input, *session, *row);
            }
            draw_series_row(row->label, 0.0f);
            if (chosen)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        if (visible.empty())
            ImGui::TextDisabled("No series matches the filter.");
        ImGui::EndCombo();
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_ScrollY;

    const float height = std::max(ImGui::GetContentRegionAvail().y
            - widget::status_foot_height(status)
            - ImGui::GetFrameHeightWithSpacing(),
        120.0f);
    if (ImGui::BeginTable("##series", 3, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed,
            80.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed,
            110.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const auto* row : visible) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(row->id.c_str());
            ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns
                    | ImGuiSelectableFlags_AllowDoubleClick);
            draw_series_row(row->label, 0.0f);
            if (ImGui::IsItemHovered()
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                state.market_selected_series_id = row->id;
                state.market_selected_series_label = row->label;
                open_chart(input, *session, *row);
            }
            ImGui::SetItemTooltip("%s", row->uri.c_str());

            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row->asset_class.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row->producer_kind.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled(
        "Double-click a series to plot it. Each plot keeps its own window.");
    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
