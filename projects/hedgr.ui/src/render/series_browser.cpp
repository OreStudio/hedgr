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
#include "implot.h"
#include <cctype>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
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
/** @brief One party in the session's picker. */
struct party_entry final {
    std::string id;
    std::string name;
};

/**
 * @brief Reads the session's party list, which the runtime serialised.
 *
 * The records are id, unit separator, name, and a record separator between
 * parties. Parsing it here keeps the model's column a flat string, which is
 * what every other session column is.
 */
[[nodiscard]] std::vector<party_entry> parties_of(std::string_view serialised) {
    std::vector<party_entry> result;
    while (!serialised.empty()) {
        const auto record_end = serialised.find('\x1e');
        const auto record = serialised.substr(0, record_end);
        const auto unit = record.find('\x1f');
        if (unit != std::string_view::npos) {
            result.push_back(party_entry{
                .id = std::string{record.substr(0, unit)},
                .name = std::string{record.substr(unit + 1)}});
        }
        if (record_end == std::string_view::npos)
            break;
        serialised.remove_prefix(record_end + 1);
    }
    return result;
}

void draw_series_row(std::string_view label, float extra_indent,
    ImTextureID base_texture, ImTextureID quote_texture) {
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_size = ImGui::GetItemRectSize();
    const auto draw = ImGui::GetWindowDrawList();
    const auto pair = hedgr::ui::widget::currencies_of(label);
    float x = rect_min.x + 4.0f + extra_indent;
    const float flag_y = rect_min.y + (rect_size.y - hedgr::ui::widget::flag_height) * 0.5f;
    if (!pair.base.empty()) {
        hedgr::ui::widget::draw_flag(draw, ImVec2(x, flag_y), pair.base, 1.0f,
            base_texture);
        x += hedgr::ui::widget::flag_width + 2.0f;
    }
    if (!pair.quote.empty()) {
        hedgr::ui::widget::draw_flag(draw, ImVec2(x, flag_y), pair.quote, 1.0f,
            quote_texture);
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


/** @brief A tenor's rank in days, for ordering a curve's nodes. */
[[nodiscard]] int tenor_rank(std::string_view tenor) {
    if (tenor.empty())
        return 0;
    int amount = 0;
    std::size_t i = 0;
    while (i < tenor.size()
        && std::isdigit(static_cast<unsigned char>(tenor[i]))) {
        amount = amount * 10 + (tenor[i] - '0');
        ++i;
    }
    if (amount == 0)
        amount = 1;
    if (i >= tenor.size())
        return amount;
    switch (static_cast<char>(std::toupper(
        static_cast<unsigned char>(tenor[i])))) {
    case 'D':
        return amount;
    case 'W':
        return amount * 7;
    case 'M':
        return amount * 30;
    case 'Y':
        return amount * 365;
    default:
        return amount;
    }
}

/** @brief The tenors of one curve, as the chooser lists them. */
struct curve_group final {
    std::string key;
    std::string label;
    std::vector<const domain::market_series_row*> members;
};

[[nodiscard]] std::vector<curve_group> group_curves(
    const std::vector<const domain::market_series_row*>& rows) {
    std::map<std::string, curve_group> grouped;
    for (const auto* row : rows) {
        if (row->asset_class != "IR")
            continue;
        if (row->curve_key.empty()) {
            /* A composite curve carries its terms in its observations. */
            auto& group = grouped[row->id];
            group.key = row->id;
            group.label = row->curve_label.empty() ? row->label
                                                   : row->curve_label;
            group.members.push_back(row);
            continue;
        }
        auto& group = grouped[row->curve_key];
        group.key = row->curve_key;
        group.label = row->curve_label;
        group.members.push_back(row);
    }
    std::vector<curve_group> curves;
    for (auto& [key, group] : grouped) {
        std::sort(group.members.begin(), group.members.end(),
            [](const auto* a, const auto* b) {
                return tenor_rank(a->tenor) < tenor_rank(b->tenor);
            });
        curves.push_back(std::move(group));
    }
    std::sort(curves.begin(), curves.end(),
        [](const curve_group& a, const curve_group& b) {
            return a.label < b.label;
        });
    return curves;
}

void request_curve(const render_input& input,
    hedgr::model::state::session_handle session,
    const curve_group& curve) {
    if (curve.members.empty())
        return;
    command request;
    request.kind = command_kind::load_term_structure;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.curve_key = curve.key;
    request.curve_label = curve.label;
    for (const auto* member : curve.members) {
        request.member_ids.push_back(member->id);
        request.member_tenors.push_back(member->tenor);
    }
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
    const std::string instance_id{input.sessions.instance_id(*session)};
    const std::string account_id{input.sessions.account_id(*session)};
    const auto* catalogue =
        input.market.catalogue(instance_id, account_id);
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

    const std::string session_party{input.sessions.party_id(*session)};
    const std::string filter{state.filter.data()};

    /*
     * The parties this login may act as, from the sign-in answer. The session
     * is signed in as one of them and this window starts on it. A login whose
     * answer carried no list still has the party it is scoped to, so the
     * picker is never empty.
     */
    auto parties = parties_of(input.sessions.parties(*session));
    if (parties.empty() && !session_party.empty()) {
        parties.push_back(party_entry{.id = session_party,
            .name = std::string{input.sessions.party_name(*session)}});
    }
    if (state.market_party_id.empty()) {
        state.market_party_id = session_party.empty()
            ? (parties.empty() ? std::string{} : parties.front().id)
            : session_party;
    }

    if (!parties.empty()) {
        std::string current{state.market_party_id};
        for (const auto& party : parties) {
            if (party.id == state.market_party_id)
                current = party.name;
        }
        const std::string session_party_name{
            input.sessions.party_name(*session)};

        ImGui::TextUnformatted("Party");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo("##party", current.c_str())) {
            for (const auto& party : parties) {
                const bool chosen = party.id == state.market_party_id;
                if (ImGui::Selectable(party.name.c_str(), chosen))
                    state.market_party_id = party.id;
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
    std::vector<std::string> asset_classes;
    for (const auto& row : rows) {
        if (row.asset_class.empty())
            continue;
        if (std::find(asset_classes.begin(), asset_classes.end(),
                row.asset_class) == asset_classes.end())
            asset_classes.push_back(row.asset_class);
    }
    std::sort(asset_classes.begin(), asset_classes.end());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const char* asset_preview = state.market_asset_class.empty()
        ? "All assets" : state.market_asset_class.c_str();
    if (ImGui::BeginCombo("##asset-class", asset_preview)) {
        if (ImGui::Selectable("All assets", state.market_asset_class.empty()))
            state.market_asset_class.clear();
        for (const auto& asset_class : asset_classes) {
            const bool chosen = asset_class == state.market_asset_class;
            if (ImGui::Selectable(asset_class.c_str(), chosen))
                state.market_asset_class = asset_class;
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (state.market_asset_class == "IR") {
        std::vector<const domain::market_series_row*> filtered;
        for (const auto& row : rows) {
            if (row.asset_class != "IR")
                continue;
            if (state.market_synthetic_only
                && row.producer_kind != "SYNTHETIC")
                continue;
            if (!state.market_party_id.empty()
                && row.party_id != state.market_party_id)
                continue;
            if (!contains_ci(row.label, filter)
                && !contains_ci(row.uri, filter))
                continue;
            filtered.push_back(&row);
        }
        const auto curves = group_curves(filtered);

        ImGui::TextUnformatted("Curve");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(520.0f);
        const char* preview = state.market_selected_curve_label.empty()
            ? "Select a term structure..." 
            : state.market_selected_curve_label.c_str();
        if (ImGui::BeginCombo("##curve-combo", preview,
                ImGuiComboFlags_HeightLarge)) {
            for (const auto& curve : curves) {
                ImGui::PushID(curve.key.c_str());
                const bool chosen = curve.key
                    == state.market_selected_curve_key;
                if (ImGui::Selectable("##curve-row", chosen)) {
                    state.market_selected_curve_key = curve.key;
                    state.market_selected_curve_label = curve.label;
                    request_curve(input, *session, curve);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(curve.label.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%zu tenors", curve.members.size());
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
        const float table_height = std::clamp(
            ImGui::GetContentRegionAvail().y * 0.35f, 120.0f, 200.0f);
        if (ImGui::BeginTable("##curves", 3, table_flags,
                ImVec2(0.0f, table_height))) {
            ImGui::TableSetupColumn("Curve",
                ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Tenors",
                ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Source",
                ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (const auto& curve : curves) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(curve.key.c_str());
                const bool chosen = curve.key
                    == state.market_selected_curve_key;
                ImGui::Selectable(curve.label.c_str(), chosen,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemClicked()) {
                    state.market_selected_curve_key = curve.key;
                    state.market_selected_curve_label = curve.label;
                    request_curve(input, *session, curve);
                }
                ImGui::SetItemTooltip("%s", curve.key.c_str());
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%zu", curve.members.size());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", curve.members.front()
                        ->producer_kind.c_str());
            }
            ImGui::EndTable();
        }

        if (!state.market_selected_curve_key.empty()) {
            const auto* curve = input.term_structures.get(
                input.sessions.instance_id(*session),
                input.sessions.account_id(*session),
                state.market_selected_curve_key);
            ImGui::TextUnformatted(state.market_selected_curve_label.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh curve")) {
                for (const auto& group : curves) {
                    if (group.key == state.market_selected_curve_key) {
                        request_curve(input, *session, group);
                        break;
                    }
                }
            }
            if (curve != nullptr && !curve->error.empty()) {
                ImGui::TextDisabled("%s", curve->error.c_str());
            } else if (curve == nullptr || curve->nodes.empty()) {
                ImGui::TextDisabled("Reading the curve...");
            } else {
                auto nodes = curve->nodes;
                std::sort(nodes.begin(), nodes.end(),
                    [](const auto& a, const auto& b) {
                        return tenor_rank(a.tenor) < tenor_rank(b.tenor);
                    });
                std::vector<double> xs;
                std::vector<double> ys;
                std::vector<const char*> labels;
                xs.reserve(nodes.size());
                ys.reserve(nodes.size());
                labels.reserve(nodes.size());
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    xs.push_back(static_cast<double>(i));
                    ys.push_back(nodes[i].value);
                    labels.push_back(nodes[i].tenor.c_str());
                }
                const float plot_height = std::max(140.0f,
                    ImGui::GetContentRegionAvail().y
                        - widget::status_foot_height(status) - 8.0f);
                if (ImPlot::BeginPlot("##term-structure",
                        ImVec2(-1, plot_height))) {
                    ImPlot::SetupAxes("Tenor", "Rate");
                    ImPlot::SetupAxisTicks(ImAxis_X1, xs.data(),
                        static_cast<int>(xs.size()), labels.data());
                    ImPlot::PlotLine("Rate", xs.data(), ys.data(),
                        static_cast<int>(xs.size()));
                    ImPlot::EndPlot();
                }
            }
        }
        ImGui::TextDisabled(
            "Choose a curve to read its latest term structure.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    /*
     * The filtered series, in one vector so the combobox and the table list
     * exactly the same rows. The combobox is the quick way in; the table is the
     * overview, and a row in either opens the same chart.
     */
    std::vector<const domain::market_series_row*> visible;
    visible.reserve(rows.size());
    for (const auto& row : rows) {
        if (!state.market_asset_class.empty()
            && row.asset_class != state.market_asset_class) {
            continue;
        }
        if (state.market_synthetic_only
            && row.producer_kind != "SYNTHETIC") {
            continue;
        }
        if (!state.market_party_id.empty()
            && row.party_id != state.market_party_id) {
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
            request_flags(input, *session, row->label);
            const auto pair = hedgr::ui::widget::currencies_of(row->label);
            draw_series_row(row->label, 0.0f,
                image_texture(input, *session, pair.base),
                image_texture(input, *session, pair.quote));
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
            request_flags(input, *session, row->label);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(row->id.c_str());
            ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns
                    | ImGuiSelectableFlags_AllowDoubleClick);
            const auto pair = hedgr::ui::widget::currencies_of(row->label);
            draw_series_row(row->label, 0.0f,
                image_texture(input, *session, pair.base),
                image_texture(input, *session, pair.quote));
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
