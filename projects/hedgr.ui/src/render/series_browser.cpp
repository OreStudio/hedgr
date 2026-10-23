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
#include "hedgr.ui/render/series_browser_view.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
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

float draw_series_row(std::string_view label, float extra_indent,
    ImTextureID base_texture, ImTextureID quote_texture) {
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_size = ImGui::GetItemRectSize();
    const auto draw = ImGui::GetWindowDrawList();
    float x = rect_min.x + 4.0f + extra_indent;
    const float flag_h = ImGui::GetTextLineHeight();
    const float flag_y = rect_min.y + (rect_size.y - flag_h) * 0.5f;
    const float flags_width = hedgr::ui::widget::draw_flag_pair(draw,
        ImVec2(x, flag_y), flag_h, base_texture, quote_texture);
    x += flags_width > 0.0f ? flags_width + 4.0f : 4.0f;
    const float text_y =
        rect_min.y + (rect_size.y - ImGui::GetTextLineHeight()) * 0.5f;
    draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text),
        label.data(), label.data() + label.size());
    return x + ImGui::CalcTextSize(label.data(),
        label.data() + label.size()).x;
}

}

namespace hedgr::ui::render {

namespace {

/** @brief The row's flat icon that opens its mini plot, true when pressed. */
bool mini_plot_button() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool pressed = ImGui::SmallButton(icon::dashboard);
    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("%s", "Open a mini plot for this series");
    return pressed;
}

}

void request_series_window(const render_input& input,
    const hedgr::model::state::session_handle session,
    const domain::window_kind kind, std::string_view series_id,
    std::string_view label, std::optional<std::int64_t> span_ns) {
    command request;
    request.kind = command_kind::open_window;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.new_window_kind = kind;
    request.series_id = std::string{series_id};
    request.series_label = std::string{label};
    request.series_span_ns = span_ns;
    input.commands.push(std::move(request));
}

void open_curve(const render_input& input,
    hedgr::model::state::session_handle session,
    const domain::market_series_row& row) {
    const auto& label = curve_label_of(row);
    command request;
    request.kind = command_kind::open_window;
    request.new_window_kind = domain::window_kind::term_structure;
    request.series_id = row.id;
    request.series_label = label;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.curve_key = row.id;
    request.curve_label = label;
    request.member_ids.push_back(row.id);
    request.member_tenors.push_back({});
    input.commands.push(std::move(request));
}

void render_series_browser(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);

    ImGui::PushID("series-browser");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto session = signed_in_session(input, handle);
    if (!session) {
        draw_signed_out(input, status, foot);
        ImGui::PopID();
        return;
    }

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##filter", "filter by series, asset, provider or URI",
        state.filter.data(), state.filter.size());
    ImGui::SameLine();
    if (ImGui::Button(icon::refresh))
        input.commands.push(command{
            .kind = command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    ImGui::SetItemTooltip("Read the series catalogue again");
    const auto* catalogue = input.market.catalogue(
        input.sessions.instance_id(*session),
        input.sessions.account_id(*session));
    static const std::vector<domain::market_series_row> none;
    const auto& rows = catalogue != nullptr ? catalogue->series : none;

    ImGui::SameLine();
    ImGui::TextDisabled("%zu in tenant", rows.size());

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

    const auto session_party = input.sessions.party_id(*session);
    const std::string_view filter{state.filter.data()};
    const std::uint64_t generation =
        catalogue != nullptr ? catalogue->generation : 0;

    /*
     * The parties this login may act as, from the sign-in answer. The session
     * is signed in as one of them and this window starts on it. A login whose
     * answer carried no list still has the party it is scoped to, so the
     * picker is never empty.
     */
    const auto& parties = state.parties;
    update_party_list(state.parties, input.sessions.parties(*session),
        session_party, input.sessions.party_name(*session));
    if (state.market_party_id.empty()) {
        state.market_party_id = session_party.empty()
            ? (parties.empty() ? std::string{} : parties.id(0))
            : std::string{session_party};
    }

    if (!parties.empty()) {
        const char* current = state.market_party_id.c_str();
        for (std::size_t i = 0; i < parties.size(); ++i) {
            if (state.market_party_id == parties.id(i))
                current = parties.name(i);
        }
        const std::string session_party_name{
            input.sessions.party_name(*session)};

        ImGui::AlignTextToFramePadding();
        widget::caption("Party");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo("##party", current)) {
            for (std::size_t i = 0; i < parties.size(); ++i) {
                const bool chosen = state.market_party_id == parties.id(i);
                if (ImGui::Selectable(parties.name(i), chosen))
                    state.market_party_id = parties.id(i);
                if (chosen)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "The party whose series this window lists. The session is signed in "
            "as %s; choosing another party shows its series in this window.",
            session_party_name.c_str());
    }

    /*
     * Asset class is the first cut. Choosing IR switches the list from points
     * to term structures: the per-tenor series of one curve become one row.
     */
    update_asset_classes(state.series_view, generation, rows);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const char* asset_preview = state.market_asset_class.empty()
        ? "All assets" : state.market_asset_class.c_str();
    if (ImGui::BeginCombo("##asset-class", asset_preview)) {
        if (ImGui::Selectable("All assets", state.market_asset_class.empty()))
            state.market_asset_class.clear();
        for (const auto at : state.series_view.classes) {
            const auto& asset_class = rows[at].asset_class;
            const bool chosen = asset_class == state.market_asset_class;
            if (ImGui::Selectable(asset_class.c_str(), chosen))
                state.market_asset_class = asset_class;
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (ImGui::Button("Cross rates")) {
        state.crm_picker_open = true;
        state.crm_picker_requested = false;
    }
    ImGui::SetItemTooltip(
        "Open a cross-rates matrix for this session's party.");

    if (state.crm_picker_open) {
        ImGui::OpenPopup("Choose CRM");
        if (ImGui::BeginPopup("Choose CRM")) {
            ImGui::TextUnformatted("Choose a CRM");
            ImGui::Separator();
            const auto* names = input.crm.names(
                input.sessions.instance_id(*session),
                input.sessions.account_id(*session),
                state.market_party_id);
            if (names == nullptr) {
                ImGui::TextDisabled("Reading configured CRMs...");
                if (!state.crm_picker_requested) {
                    model::command::command request;
                    request.kind
                        = model::command::command_kind::load_crm_names;
                    request.instance_id
                        = std::string{input.sessions.instance_id(*session)};
                    request.account_id
                        = std::string{input.sessions.account_id(*session)};
                    request.party_id = state.market_party_id;
                    input.commands.push(std::move(request));
                    state.crm_picker_requested = true;
                }
            } else if (names->empty()) {
                ImGui::TextDisabled("No CRMs configured for this party.");
                if (ImGui::Button("Retry")) {
                    model::command::command request;
                    request.kind
                        = model::command::command_kind::load_crm_names;
                    request.instance_id
                        = std::string{input.sessions.instance_id(*session)};
                    request.account_id
                        = std::string{input.sessions.account_id(*session)};
                    request.party_id = state.market_party_id;
                    input.commands.push(std::move(request));
                    state.crm_picker_requested = true;
                }
            } else {
                for (const auto& name : *names) {
                    if (!ImGui::Selectable(name.c_str()))
                        continue;
                    model::command::command request;
                    request.kind = model::command::command_kind::open_window;
                    request.instance_id
                        = std::string{input.sessions.instance_id(*session)};
                    request.account_id
                        = std::string{input.sessions.account_id(*session)};
                    request.new_window_kind
                        = model::domain::window_kind::crm_matrix;
                    request.party_id = state.market_party_id;
                    request.crm_name = name;
                    input.commands.push(std::move(request));
                    state.crm_picker_open = false;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Cancel")) {
                state.crm_picker_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            state.crm_picker_open = false;
        }
    }

    if (state.market_asset_class == "IR") {
        update_series_browser_view(state.series_view,
            {.generation = generation, .filter = filter,
                .asset_class = state.market_asset_class,
                .party = state.market_party_id},
            rows);
        const auto& curves = state.series_view.rows;

        ImGui::AlignTextToFramePadding();
        widget::caption("Curve");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(520.0f);
        const char* preview = state.market_selected_curve_label.empty()
            ? "Select a term structure..." 
            : state.market_selected_curve_label.c_str();
        if (ImGui::BeginCombo("##curve-combo", preview,
                ImGuiComboFlags_HeightLarge)) {
            for (const auto at : curves) {
                const auto& curve = rows[at];
                const auto& label = curve_label_of(curve);
                ImGui::PushID(curve.id.c_str());
                const bool chosen = curve.id
                    == state.market_selected_curve_key;
                if (ImGui::Selectable("##curve-row", chosen)) {
                    state.market_selected_curve_key = curve.id;
                    state.market_selected_curve_label = label;
                    open_curve(input, *session, curve);
                }
                const float row_y = ImGui::GetItemRectMin().y;
                request_flags(input, *session, label);
                const auto pair = hedgr::ui::widget::currencies_of(label);
                const float label_end = draw_series_row(label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                ImGui::SetCursorScreenPos(ImVec2(label_end + 12.0f, row_y));
                ImGui::TextDisabled("%zu tenors", std::size_t{1});
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            if (curves.empty())
                ImGui::TextDisabled("No curves match the filters.");
            ImGui::EndCombo();
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg
            | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingStretchProp;
        const float table_height = std::max(120.0f,
            ImGui::GetContentRegionAvail().y
                - widget::status_foot_height(status)
                - ImGui::GetTextLineHeightWithSpacing() * 2.0f);
        if (ImGui::BeginTable("##curves", 3, table_flags,
                ImVec2(0.0f, table_height))) {
            ImGui::TableSetupColumn("Curve",
                ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Tenors",
                ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Provider",
                ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (const auto at : curves) {
                const auto& curve = rows[at];
                const auto& label = curve_label_of(curve);
                request_flags(input, *session, label);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(curve.id.c_str());
                const bool chosen = curve.id
                    == state.market_selected_curve_key;
                ImGui::Selectable("##curve", chosen,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick);
                const auto pair = hedgr::ui::widget::currencies_of(label);
                draw_series_row(label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                if (ImGui::IsItemHovered()
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    state.market_selected_curve_key = curve.id;
                    state.market_selected_curve_label = label;
                    open_curve(input, *session, curve);
                }
                ImGui::SetItemTooltip("%s", curve.id.c_str());
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%zu", std::size_t{1});
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", curve.provider_code.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::TextDisabled(
            "Double-click a curve to open its term structure.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    /*
     * The filtered series, in one vector so the combobox and the table list
     * exactly the same rows. The combobox is the quick way in; the table is the
     * overview, and a row in either opens the same chart.
     */
    update_series_browser_view(state.series_view,
        {.generation = generation, .filter = filter,
            .asset_class = state.market_asset_class,
            .party = state.market_party_id},
        rows);
    const auto& visible = state.series_view.rows;

    if (visible.empty() && !rows.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("No series match the filters.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear filters")) {
            std::fill(state.filter.begin(), state.filter.end(), '\0');
        }
    }

    ImGui::AlignTextToFramePadding();
    widget::caption("Series");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(420.0f);
    const char* preview = state.market_selected_series_label.empty()
        ? "Select a series..."
        : state.market_selected_series_label.c_str();
    if (ImGui::BeginCombo("##series-combo", preview,
            ImGuiComboFlags_HeightLarge)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        /* The chosen row is always drawn, so opening the combo scrolls to it. */
        for (std::size_t i = 0; i < visible.size(); ++i) {
            if (rows[visible[i]].id == state.market_selected_series_id)
                clipper.IncludeItemByIndex(static_cast<int>(i));
        }
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd;
                ++index) {
                const auto* row = &rows[visible[static_cast<std::size_t>(index)]];
                ImGui::PushID(row->id.c_str());
                const bool chosen =
                    row->id == state.market_selected_series_id;
                if (ImGui::Selectable("##combo-row", chosen)) {
                    state.market_selected_series_id = row->id;
                    state.market_selected_series_label = row->label;
                    request_series_window(input, *session, domain::window_kind::chart,
                        row->id, row->label);
                }
                request_flags(input, *session, row->label);
                const auto pair = hedgr::ui::widget::currencies_of(row->label);
                draw_series_row(row->label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
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
    if (ImGui::BeginTable("##series", 4, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed,
            80.0f);
        ImGui::TableSetupColumn("Provider", ImGuiTableColumnFlags_WidthFixed,
            110.0f);
        ImGui::TableSetupColumn("##mini", ImGuiTableColumnFlags_WidthFixed
                | ImGuiTableColumnFlags_NoHeaderLabel,
            ImGui::GetFrameHeight());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd;
                ++index) {
                const auto* row = &rows[visible[static_cast<std::size_t>(index)]];
                request_flags(input, *session, row->label);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(row->id.c_str());
                ImGui::Selectable("##row", false,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick
                        | ImGuiSelectableFlags_AllowOverlap);
                const auto pair = hedgr::ui::widget::currencies_of(row->label);
                draw_series_row(row->label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                if (ImGui::IsItemHovered()
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    state.market_selected_series_id = row->id;
                    state.market_selected_series_label = row->label;
                    request_series_window(input, *session, domain::window_kind::chart,
                        row->id, row->label);
                }
                ImGui::SetItemTooltip("%s", row->uri.c_str());

                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row->asset_class.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row->provider_code.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(row->id.c_str());
                if (mini_plot_button()) {
                    request_series_window(input, *session,
                        domain::window_kind::mini_plot, row->id, row->label,
                        chart_span_ns(input.market_view.chart_span));
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled(
        "Double-click a series to plot it. Each plot keeps its own window.");
    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
