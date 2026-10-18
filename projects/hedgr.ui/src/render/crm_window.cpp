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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

struct crm_party final {
    std::string id;
    std::string name;
};

[[nodiscard]] std::vector<crm_party> parse_parties(std::string_view text) {
    std::vector<crm_party> result;
    while (!text.empty()) {
        const auto end = text.find('\x1e');
        const auto record = text.substr(0, end);
        const auto unit = record.find('\x1f');
        if (unit != std::string_view::npos)
            result.push_back(crm_party{
                .id = std::string{record.substr(0, unit)},
                .name = std::string{record.substr(unit + 1)}});
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return result;
}

[[nodiscard]] bool matches(const std::string& text, const std::string& filter) {
    return filter.empty() || text.find(filter) != std::string::npos;
}

[[nodiscard]] std::string pair_text(const std::string& base,
    const std::string& quote) {
    return base + "/" + quote;
}

[[nodiscard]] std::string as_of_text(std::string_view iso) {
    if (iso.empty())
        return "\u2014";
    std::string text;
    for (const char c : iso) {
        if (c == 'T')
            text += ' ';
        else if (c == 'Z')
            text += " UTC";
        else
            text += c;
    }
    return text;
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
    const auto session = input.windows.session_of(handle);
    if (crm == nullptr || !session) {
        ImGui::TextWrapped("%s", "This window is not a CRM.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const auto instance_id = input.sessions.instance_id(*session);
    const auto account_id = input.sessions.account_id(*session);
    const std::string session_party{input.sessions.party_id(*session)};
    auto parties = parse_parties(input.sessions.parties(*session));
    if (parties.empty() && !session_party.empty())
        parties.push_back(crm_party{.id = session_party,
            .name = std::string{input.sessions.party_name(*session)}});
    if (!state.crm_initialised) {
        state.crm_party_id = !crm->party_id.empty()
            ? crm->party_id : session_party;
        state.crm_name = crm->crm_name;
        state.crm_reciprocal = crm->reciprocal;
        state.crm_initialised = true;
    }

    const auto* snapshot = input.crm.get(instance_id, account_id,
        state.crm_party_id);

    auto request_read = [&] {
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

    /*
     * The controls sit in two rows: the scope first, then the pair filter and
     * the refresh. A trading matrix is read left to right, so the scope is
     * where the eye lands before the numbers.
     */
    ImGui::TextUnformatted("Party");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    std::string party_name = state.crm_party_id;
    for (const auto& party : parties)
        if (party.id == state.crm_party_id)
            party_name = party.name;
    if (ImGui::BeginCombo("##crm-party", party_name.c_str())) {
        for (const auto& party : parties) {
            const bool chosen = party.id == state.crm_party_id;
            if (ImGui::Selectable(party.name.c_str(), chosen)) {
                state.crm_party_id = party.id;
                request_read();
            }
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    std::set<std::string> crm_names;
    if (snapshot != nullptr)
        for (const auto& cell : snapshot->cells)
            crm_names.insert(cell.crm_name);
    ImGui::TextUnformatted("CRM");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    const std::string crm_preview = state.crm_name.empty()
        ? "All CRMs" : state.crm_name;
    if (ImGui::BeginCombo("##crm-name", crm_preview.c_str())) {
        if (ImGui::Selectable("All CRMs", state.crm_name.empty())) {
            state.crm_name.clear();
            request_read();
        }
        for (const auto& name : crm_names) {
            const bool chosen = name == state.crm_name;
            if (ImGui::Selectable(name.c_str(), chosen)) {
                state.crm_name = name;
                request_read();
            }
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    if (ImGui::Checkbox("Reciprocal", &state.crm_reciprocal))
        request_read();
    ImGui::SetItemTooltip(
        "Fill a configured pair with 1/reverse when only the reverse has a "
        "direct quote.");
    ImGui::NewLine();
    if (ImGui::Button("Refresh"))
        request_refresh();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##crm-filter", "Filter currencies",
        state.filter.data(), state.filter.size());

    /*
     * The window polls only while it is drawn. The server computes the batch
     * on demand, so a hidden matrix must not keep a party's rate engine busy.
     */
    const double now = ImGui::GetTime();
    if (state.crm_next_poll == 0.0)
        state.crm_next_poll = now + 5.0;
    if (now >= state.crm_next_poll) {
        request_refresh();
        state.crm_next_poll = now + 5.0;
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (snapshot == nullptr || snapshot->cells.empty()) {
        if (snapshot != nullptr && !snapshot->error.empty())
            ImGui::TextColored(
                ImGui::ColorConvertU32ToFloat4(theme::failure_colour), "%s",
                snapshot->error.c_str());
        else
            ImGui::TextDisabled("Reading the CRM...");
    } else {
        std::size_t fresh = 0;
        std::size_t stale = 0;
        std::size_t unavailable = 0;
        for (const auto& cell : snapshot->cells) {
            if (!state.crm_name.empty() && cell.crm_name != state.crm_name)
                continue;
            if (cell.status == "fresh")
                ++fresh;
            else if (cell.status == "stale")
                ++stale;
            else
                ++unavailable;
        }
        const ImU32 colour = stale > 0 || unavailable > 0
            ? theme::readiness_colour(model::domain::readiness::bootstrapping)
            : theme::health_live_colour;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour),
            "\u25cf %zu fresh  %zu stale  %zu unavailable", fresh, stale,
            unavailable);
        ImGui::SameLine();
        ImGui::TextDisabled("\u00b7 as of %s",
            as_of_text(snapshot->observed_as_of).c_str());
    }

    if (snapshot == nullptr || snapshot->cells.empty()) {
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const std::string filter{state.filter.data()};
    std::set<std::string> currencies;
    std::map<std::pair<std::string, std::string>,
        const model::domain::crm_cell*> cells;
    for (const auto& cell : snapshot->cells) {
        if (!state.crm_name.empty() && cell.crm_name != state.crm_name)
            continue;
        if (!matches(cell.base_currency_code, filter)
            && !matches(cell.quote_currency_code, filter))
            continue;
        currencies.insert(cell.base_currency_code);
        currencies.insert(cell.quote_currency_code);
        const auto key = std::make_pair(cell.base_currency_code,
            cell.quote_currency_code);
        const auto found = cells.find(key);
        if (found == cells.end()
            || (found->second->status == "unavailable"
                && cell.status != "unavailable"))
            cells[key] = &cell;
    }

    if (currencies.size() < 2) {
        ImGui::TextDisabled("No pairs match the filter.");
        widget::draw_status_foot(status, input.now_ns, foot);
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
    if (ImGui::BeginTable("##crm", static_cast<int>(currencies.size()) + 1,
            flags, ImVec2(0.0f, table_height))) {
        ImGui::TableSetupColumn("Base / Quote",
            ImGuiTableColumnFlags_WidthFixed, 70.0f);
        for (const auto& currency : currencies)
            ImGui::TableSetupColumn(currency.c_str(),
                ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        for (const auto& base : currencies) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(base.c_str());
            for (const auto& quote : currencies) {
                ImGui::TableNextColumn();
                if (base == quote) {
                    ImGui::TextDisabled("\u2014");
                    continue;
                }
                const auto found = cells.find({base, quote});
                if (found == cells.end()) {
                    ImGui::TextDisabled("\u2014");
                    continue;
                }
                const auto& cell = *found->second;
                const std::string pair = pair_text(base, quote);
                const bool selected = state.crm_pair == pair;
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
                char text[48];
                std::snprintf(text, sizeof(text), "%.5f", cell.rate);
                if (ImGui::Selectable(text, selected,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    state.crm_pair = pair;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        const auto* catalogue = input.market.catalogue(
                            instance_id, account_id);
                        if (catalogue != nullptr) {
                            for (const auto& row : catalogue->series) {
                                if (row.asset_class != "FX")
                                    continue;
                                const auto base_at
                                    = row.uri.find("/fx/" + base + "?");
                                const auto quote_at
                                    = row.uri.find("ccy=" + quote);
                                if (base_at == std::string::npos
                                    || quote_at == std::string::npos)
                                    continue;
                                model::command::command chart;
                                chart.kind
                                    = model::command::command_kind::open_window;
                                chart.instance_id = std::string{instance_id};
                                chart.account_id = std::string{account_id};
                                chart.new_window_kind
                                    = model::domain::window_kind::chart;
                                chart.series_id = row.id;
                                chart.series_label = row.label;
                                input.commands.push(std::move(chart));
                                break;
                            }
                        }
                    }
                }
                ImGui::SetItemTooltip("%s  %s\n%s  %s\nas of %s", pair.c_str(),
                    cell.crm_name.c_str(), cell.status.c_str(),
                    cell.reciprocal ? "reciprocal" : "direct",
                    cell.as_of.c_str());
                if (cell.delta_pct) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        *cell.delta_pct < 0.0
                            ? ImGui::ColorConvertU32ToFloat4(
                                  theme::failure_colour)
                            : colour,
                        "%+.2f%%", *cell.delta_pct);
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
