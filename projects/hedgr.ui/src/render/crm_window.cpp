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
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/crm_grid.hpp"
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

/**
 * @brief One verdict's count as a pill, in the verdict's tone while any cell
 * carries it and quiet at zero, so an empty bucket claims no state and steps
 * back behind the buckets that hold cells.
 */
void count_pill(std::string_view verdict, std::size_t count,
    theme::tone tone, const char* help) {
    std::array<char, 32> text{};
    const auto written = std::format_to_n(text.data(),
        static_cast<std::ptrdiff_t>(text.size()), "{} {}", count, verdict);
    widget::value_pill(
        std::string_view(text.data(),
            static_cast<std::size_t>(written.out - text.data())),
        count > 0 ? tone : theme::tone::quiet);
    ImGui::SetItemTooltip("%s", help);
}

/** @brief Opens a mini plot card for each series held in the window's state. */
void open_pending_cards(const render_input& input,
    const hedgr::model::state::session_handle session, window_ui_state& state) {
    for (const auto& card : state.crm_pending_cards)
        request_series_window(input, session,
            model::domain::window_kind::mini_plot, card.id, card.label);
    state.crm_pending_cards.clear();
}

/** @brief The providers the catalogue says publish definitions only. */
[[nodiscard]] std::vector<std::string> reference_provider_codes(
    const model::state::series_catalogue* catalogue) {
    std::vector<std::string> codes;
    if (catalogue == nullptr)
        return codes;
    for (const auto& provider : catalogue->providers)
        if (model::domain::is_reference_provider(provider))
            codes.push_back(provider.code);
    return codes;
}

/** @brief The confirmation a set above the limit waits behind. */
void draw_mini_plot_confirmation(const render_input& input,
    const hedgr::model::state::session_handle session, window_ui_state& state) {
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Open mini plots?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::Text("This opens %zu mini plot cards.",
        state.crm_pending_cards.size());
    if (state.crm_unmatched_pairs > 0)
        ImGui::TextDisabled("%zu pairs have no series and are skipped.",
            state.crm_unmatched_pairs);
    if (ImGui::Button("Open")) {
        open_pending_cards(input, session, state);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.crm_pending_cards.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}

void render_crm_matrix(window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    ImGui::PushID("crm-matrix");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* crm = std::get_if<model::domain::crm_matrix_state>(&shape);
    if (crm == nullptr) {
        widget::draw_view_state_body(status, foot,
            {widget::view_state::error, widget::view_words::read_failed,
                "This window does not hold a CRM."});
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }
    const auto session = signed_in_session(input, handle);
    if (!session) {
        draw_session_state(input, handle, status, foot);
        ImGui::PopID();
        return;
    }

    const auto instance_id = input.sessions.instance_id(*session);
    const auto account_id = input.sessions.account_id(*session);
    const std::string session_party{input.sessions.party_id(*session)};
    if (!state.crm_initialised) {
        state.crm_party_id = !crm->party_id.empty()
            ? crm->party_id : session_party;
        state.crm_name = crm->crm_name;
        state.crm_reciprocal = crm->reciprocal;
        state.crm_initialised = true;
    }

    if (state.crm_name.empty()) {
        widget::draw_view_state_body(status, foot,
            {widget::view_state::no_data, widget::view_words::empty,
                "No CRM is selected."});
        static_cast<void>(
            draw_foot_with_party(input, *session, status, foot, state));
        ImGui::PopID();
        return;
    }

    const auto* snapshot = input.crm.get(instance_id, account_id,
        state.crm_party_id, state.crm_name, state.crm_reciprocal);

    const auto as_of_ns = snapshot != nullptr
        ? snapshot->observed_as_of_ns : 0;
    const bool reading = snapshot != nullptr && snapshot->reading;
    set_health_foot(foot, model::domain::data_health::unavailable, as_of_ns,
        input.wall_now_ns, reading);

    auto configure_scope = [&] {
        model::command::command request;
        request.kind = model::command::command_kind::configure_crm;
        request.window_id = std::string{input.windows.id(handle)};
        request.party_id = state.crm_party_id;
        request.crm_name = state.crm_name;
        request.crm_reciprocal = state.crm_reciprocal;
        input.commands.push(std::move(request));
        state.crm_pair.clear();
    };

    auto request_refresh = [&] {
        model::command::command request;
        request.kind = model::command::command_kind::refresh_window;
        request.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(request));
    };

    ImGui::AlignTextToFramePadding();
    widget::caption("CRM",
        "The currency rate matrix shown. The pills count its pairs by how "
        "fresh their rates are.");
    ImGui::SameLine();
    widget::value_pill(state.crm_name);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (ImGui::Checkbox("Reciprocal", &state.crm_reciprocal))
        configure_scope();
    ImGui::SetItemTooltip(
        "Fill a configured pair with 1/reverse when only the reverse has a "
        "direct quote.");
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        request_refresh();
    ImGui::SameLine();
    if (ImGui::Button("Mini plots")) {
        const auto* catalogue = input.market.catalogue(instance_id,
            account_id);
        state.crm_pending_cards = catalogue == nullptr
            ? std::vector<crm_card>{}
            : crm_pair_cards(catalogue->series, state.crm_party_id,
                  state.crm_view.currencies, state.crm_unmatched_pairs,
                  reference_provider_codes(catalogue));
        if (state.crm_pending_cards.size() > crm_mini_plot_confirm_above)
            ImGui::OpenPopup("Open mini plots?");
        else
            open_pending_cards(input, *session, state);
    }
    ImGui::SetItemTooltip(
        "Open a mini plot card for every pair in this matrix. Pairs with no "
        "series are skipped.");
    draw_mini_plot_confirmation(input, *session, state);

    if (snapshot == nullptr || snapshot->cells.empty()) {
        auto facts = session_view_facts(input, handle);
        facts.reading = snapshot == nullptr || snapshot->reading;
        facts.read_failure = snapshot != nullptr
            ? std::string_view{snapshot->error} : std::string_view{};
        facts.has_data = false;
        facts.empty_what = "No rates are configured for this party.";
        const auto verdict = widget::view_state_of(facts);
        trace_view_state("cross rates", state.crm_name, state, verdict);
        widget::draw_view_state_body(status, foot, verdict);
        static_cast<void>(
            draw_foot_with_party(input, *session, status, foot, state));
        ImGui::PopID();
        return;
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    {
        if (state.crm_view.version != snapshot->version)
            state.crm_view = build_crm_grid(*snapshot, state.crm_name,
                state.crm_currencies);
        const auto& counts = state.crm_view.counts;

        /*
         * The cells carry the deployment's own verdict on each pair, so the
         * footer uses the same answer rather than judging the batch's as-of
         * again. A batch that keeps returning fresh cells is live even when
         * the observation time it quotes has not moved.
         */
        const auto health = model::domain::assess_verdicts(counts);
        set_health_foot(foot, health, as_of_ns, input.wall_now_ns, reading);
        count_pill("fresh", counts.fresh, theme::tone::good,
            "Pairs whose rate is current.");
        ImGui::SameLine();
        count_pill("stale", counts.stale, theme::tone::warning,
            "Pairs whose rate is older than it should be.");
        ImGui::SameLine();
        count_pill("unavailable",
            counts.disconnected + counts.unavailable, theme::tone::neutral,
            "Pairs with no rate, or whose source cannot be reached.");
        if (snapshot->latency_ns > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("\u00b7 %.0f ms",
                static_cast<double>(snapshot->latency_ns) / 1.0e6);
        }
    }

    const auto& grid = state.crm_view;
    const auto& currencies = grid.currencies;
    if (currencies.size() < 2) {
        widget::draw_view_state_body(status, foot,
            {widget::view_state::no_data, widget::view_words::empty,
                "This CRM has no pairs."});
        static_cast<void>(
            draw_foot_with_party(input, *session, status, foot, state));
        ImGui::PopID();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
    const auto flags = ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_SizingFixedFit;
    const float table_height = std::max(
        ImGui::GetContentRegionAvail().y
            - widget::status_foot_height(status)
            - ImGui::GetStyle().ItemSpacing.y,
        160.0f);
    for (const auto& currency : currencies)
        request_flag(input, *session, currency);

    if (ImGui::BeginTable("##crm", static_cast<int>(currencies.size()) + 1,
            flags, ImVec2(0.0f, table_height))) {
        ImGui::TableSetupColumn("Base / Quote",
            ImGuiTableColumnFlags_WidthFixed, 78.0f);
        for (const auto& currency : currencies)
            ImGui::TableSetupColumn(currency.c_str(),
                ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupScrollFreeze(1, 1);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Base / Quote");
        for (const auto& currency : currencies) {
            ImGui::TableNextColumn();
            const auto flag = image_texture(input, *session, currency);
            if (flag != 0) {
                ImGui::Image(static_cast<ImTextureID>(flag),
                    ImVec2(16.0f, 11.0f));
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::TextUnformatted(currency.c_str());
        }

        /*
         * A cell has a fixed two-line height. The delta line is always
         * reserved, so a tick that removes or adds a delta cannot change the
         * row height and move the grid under the pointer.
         */
        const float cell_line = ImGui::GetTextLineHeight();
        const float row_height = cell_line * 2.0f
            + ImGui::GetStyle().CellPadding.y * 2.0f + 4.0f;
        std::size_t at = 0;
        for (const auto& base : currencies) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
            ImGui::TableNextColumn();
            const auto flag = image_texture(input, *session, base);
            if (flag != 0) {
                ImGui::Image(static_cast<ImTextureID>(flag),
                    ImVec2(16.0f, 11.0f));
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::TextUnformatted(base.c_str());
            for (const auto& quote : currencies) {
                const auto& entry = grid.cells[at++];
                ImGui::TableNextColumn();
                if (base == quote) {
                    ImGui::TextColored(ImVec4(0.30f, 0.33f, 0.37f, 1.0f),
                        "\u2014");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Same currency");
                    continue;
                }
                if (entry.cell == nullptr) {
                    ImGui::TextDisabled("\u2014");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("No configured rate for %s",
                            entry.pair.c_str());
                    continue;
                }
                const auto& cell = *entry.cell;
                if (cell.status == "unavailable") {
                    ImGui::TextDisabled("\u2014");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s  %s\n%s\n%s",
                            entry.pair.c_str(), cell.crm_name.c_str(),
                            cell.status.c_str(), "No direct or derived quote");
                    continue;
                }
                const auto& pair = entry.pair;
                const bool selected = state.crm_pair == pair;
                const double delta = cell.delta_pct.value_or(0.0);
                if (!selected && !crm_delta_is_zero(delta)
                    && std::abs(delta) > 0.0) {
                    const float strength = static_cast<float>(std::min(
                        std::abs(delta) / 0.05, 1.0));
                    const auto tint = delta < 0.0
                        ? IM_COL32(255, 80, 90,
                              8 + static_cast<int>(24 * strength))
                        : IM_COL32(80, 220, 140,
                              8 + static_cast<int>(24 * strength));
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
                }
                if (selected)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                        ImGui::GetColorU32(ImGuiCol_Header));

                const ImVec4 colour = cell.status == "fresh"
                    ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                    : cell.status == "stale"
                    ? ImGui::ColorConvertU32ToFloat4(
                          theme::readiness_colour(
                              model::domain::readiness::bootstrapping))
                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const float rate_width
                    = ImGui::CalcTextSize(entry.rate.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                    + ImGui::GetContentRegionAvail().x - rate_width);
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                ImGui::PushFont(icon::numeric_font());
                if (ImGui::Selectable(entry.label.c_str(), selected,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    state.crm_pair = pair;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                        && !cell.reciprocal) {
                        const auto* catalogue = input.market.catalogue(
                            instance_id, account_id);
                        if (catalogue != nullptr) {
                            if (const auto* row = crm_series_for_pair(
                                    catalogue->series, state.crm_party_id,
                                    base, quote,
                                    reference_provider_codes(catalogue))) {
                                model::command::command chart;
                                chart.kind
                                    = model::command::command_kind::open_window;
                                chart.instance_id = std::string{instance_id};
                                chart.account_id = std::string{account_id};
                                chart.new_window_kind
                                    = model::domain::window_kind::chart;
                                chart.series_id = row->id;
                                chart.series_label = row->label;
                                input.commands.push(std::move(chart));
                            }
                        }
                    }
                }
                ImGui::PopFont();
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s\nprovider %s  %s\n%s  %s\nas of %s",
                    pair.c_str(), cell.crm_name.c_str(), cell.status.c_str(),
                    cell.reciprocal ? "reciprocal" : "direct",
                    "double-click to open its FX chart", cell.as_of.c_str());
                if (cell.delta_pct) {
                    char delta_text[32];
                    std::snprintf(delta_text, sizeof(delta_text), "%+.2f%%",
                        delta);
                    const float delta_width
                        = ImGui::CalcTextSize(delta_text).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                        + ImGui::GetContentRegionAvail().x - delta_width);
                    if (crm_delta_is_zero(delta)) {
                        ImGui::TextDisabled("0.00%%");
                    } else {
                        ImGui::TextColored(
                            delta < 0.0
                                ? ImGui::ColorConvertU32ToFloat4(
                                      theme::failure_colour)
                                : ImGui::ColorConvertU32ToFloat4(
                                      theme::health_live_colour),
                            "%s", delta_text);
                    }
                } else {
                    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    static_cast<void>(draw_foot_with_party(input, *session, status, foot, state));
    ImGui::PopID();
}

}
