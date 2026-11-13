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
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/series_browser_view.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include "ores.logging/make_logger.hpp"
#include "implot.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
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

/** @brief This file's logger: which rungs a curve opens with, and why. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.series_browser");
    return instance;
}

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

/** @brief Whether @p text holds @p needle, ignoring case and an empty needle. */
[[nodiscard]] bool contains_folded(std::string_view text,
    std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    };
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(text[i + j]) != lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
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

/**
 * @brief The published curves whose name matches @p filter, by name.
 *
 * They are listed beside the tick curves rather than filtered by provider or
 * party: a published curve is one object the deployment calibrates, not a
 * series under a provider.
 */
/**
 * @brief The published curves @p party may act on, matching @p filter.
 *
 * The deployment lists a published curve once per party that holds it, so the
 * same name arrives several times and only one of the copies is the session's.
 * A published curve is not a catalogue row, but the series it carries usually
 * is: the row's party is the copy's party, and that is what tells them apart. A
 * curve whose series the catalogue does not list is kept, because nothing says
 * it belongs to another party, and dropping it would hide a curve that may well
 * be the session's own.
 */
[[nodiscard]] std::vector<domain::published_curve> published_curves_matching(
    const hedgr::model::state::series_catalogue* catalogue,
    std::string_view party, std::string_view filter) {
    std::vector<domain::published_curve> matching;
    if (catalogue == nullptr)
        return matching;
    for (const auto& curve : catalogue->published_curves) {
        if (!contains_folded(curve.label, filter))
            continue;
        if (!party.empty()) {
            const auto* row = catalogue->row_of(curve.series_id);
            if (row != nullptr && row->party_id != party)
                continue;
        }
        matching.push_back(curve);
    }
    std::sort(matching.begin(), matching.end(),
        [](const auto& left, const auto& right) {
            return left.label < right.label;
        });
    return matching;
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
    const auto instance_id = std::string{input.sessions.instance_id(session)};
    const auto account_id = std::string{input.sessions.account_id(session)};

    /*
     * The curve is every series of one instrument, not the one row the operator
     * clicked: an IR series is a point, and what a person reads is the shape
     * those points make together. Which field names the rungs is the
     * catalogue's answer and not an assumption - a swap ladder differs by
     * `fwd_start`, not by `tenor` - so the grouping lives in one function that
     * a test can check against real rows.
     */
    curve_group group;
    if (const auto* catalogue = input.market.catalogue(instance_id, account_id))
        group = curve_of(catalogue->series, row);
    if (group.members.empty()) {
        group.members.push_back(&row);
        group.rungs.push_back(row.tenor);
    }

    command request;
    request.kind = command_kind::open_window;
    request.new_window_kind = domain::window_kind::term_structure;
    request.series_id = row.id;
    request.series_label = label;
    request.instance_id = instance_id;
    request.account_id = account_id;
    request.curve_key = row.curve_key.empty() ? row.id : row.curve_key;
    request.curve_label = label;
    request.member_ids.reserve(group.members.size());
    request.member_tenors.reserve(group.members.size());
    for (std::size_t i = 0; i < group.members.size(); ++i) {
        request.member_ids.push_back(group.members[i]->id);
        request.member_tenors.push_back(group.rungs[i]);
    }
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Curve '" << label << "' (" << row.id << ") opens with "
        << group.members.size() << " rungs"
        << (group.field.empty() ? std::string{} : " by '" + group.field + "'");
    input.commands.push(std::move(request));
}

void request_published_curve_window(const render_input& input,
    hedgr::model::state::session_handle session,
    const domain::published_curve& curve) {
    /*
     * No members: the window reads the deployment's published curve sets
     * rather than assembling a ladder from one series per tenor, which is what
     * an empty member list tells the read to do.
     */
    command request;
    request.kind = command_kind::open_window;
    request.new_window_kind = domain::window_kind::term_structure;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.series_id = curve.series_id;
    request.series_label = curve.label;
    request.curve_key = curve.series_id;
    request.curve_label = curve.label;
    input.commands.push(std::move(request));
}

/**
 * @brief Opens one published volatility surface, by the blueprint it is under.
 *
 * The blueprint is the window's identity, so requesting the same one twice
 * focuses the window already drawn. The pair travels with the command because
 * the title and the flags need it before any read answers.
 */
void request_vol_surface_window(const render_input& input,
    hedgr::model::state::session_handle session,
    const domain::surface_blueprint& surface) {
    command request;
    request.kind = command_kind::open_window;
    request.new_window_kind = domain::window_kind::vol_surface;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.series_id = surface.id;
    request.series_label = surface.name;
    request.blueprint_id = surface.id;
    request.curve_label = surface.name;
    request.base_currency = surface.base_currency;
    request.quote_currency = surface.quote_currency;
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
        draw_session_state(input, handle, status, foot);
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

    /*
     * The providers the catalogue read brought with it. A reference provider
     * defines an instrument and never ticks, so its rows are hidden unless
     * VIEW > Reference providers is on; an empty provider list hides nothing.
     */
    if (catalogue != nullptr
        && state.reference_generation != catalogue->generation) {
        state.reference_generation = catalogue->generation;
        state.reference_providers.clear();
        for (const auto& provider : catalogue->providers)
            if (domain::is_reference_provider(provider))
                state.reference_providers.push_back(provider.code);
    }
    const std::span<const std::string> hidden_providers =
        input.market_view.show_reference_providers
        ? std::span<const std::string>{}
        : std::span<const std::string>{state.reference_providers};

    ImGui::SameLine();
    ImGui::TextDisabled("%zu in tenant", rows.size());

    const auto catalogue_state = catalogue != nullptr
        ? catalogue->state
        : hedgr::model::state::catalogue_state::unread;
    /*
     * The catalogue's state is the whole body's state while it holds no rows.
     * A failed refresh over rows already held keeps the list and says ERROR on
     * the foot.
     */
    namespace cat = hedgr::model::state;
    auto catalogue_facts = session_view_facts(input, handle);
    catalogue_facts.reading = rows.empty()
        && (catalogue_state == cat::catalogue_state::reading
            || catalogue_state == cat::catalogue_state::unread);
    catalogue_facts.read_failure =
        catalogue_state == cat::catalogue_state::failed
        ? std::string_view{catalogue->error} : std::string_view{};
    catalogue_facts.has_data = !rows.empty();
    catalogue_facts.empty_what =
        "This deployment holds no market series for this session.";
    const auto catalogue_verdict = widget::view_state_of(catalogue_facts);
    trace_view_state("market data", "catalogue", state, catalogue_verdict);
    widget::set_view_foot(foot, catalogue_verdict);

    const auto session_party = input.sessions.party_id(*session);
    const std::string_view filter{state.filter.data()};
    const std::uint64_t generation =
        catalogue != nullptr ? catalogue->generation : 0;

    /*
     * The parties this login may act as, from the sign-in answer, and the one
     * it acts as now. A login whose answer carried no list still has the party
     * it is scoped to, so the list is never empty while a party is held.
     *
     * There is one party at a time per session: this window follows it rather
     * than filtering by another, and the control at the foot is what changes
     * it. Filtering by a party the session does not hold would list series
     * whose ticks then had to be read with a token for another party.
     */
    update_party_list(state.parties, input.sessions.parties(*session),
        session_party, input.sessions.party_name(*session));
    state.market_party_id = session_party.empty()
        ? (state.parties.empty()
                  ? std::string{}
                  : std::string{state.parties.id(0)})
        : std::string{session_party};
    const auto draw_foot = [&] {
        static_cast<void>(
            draw_foot_with_party(input, *session, status, foot, state));
    };

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

    /*
     * The provider cut. The same instrument is listed once per provider and
     * only a data vendor's series tick, so this narrows the list to one.
     */
    update_providers(state.series_view, generation, rows);
    const auto hidden_provider = [&](std::string_view code) {
        return std::find(hidden_providers.begin(), hidden_providers.end(),
                   code)
            != hidden_providers.end();
    };
    if (hidden_provider(state.market_provider))
        state.market_provider.clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    const char* provider_preview = state.market_provider.empty()
        ? "All providers" : state.market_provider.c_str();
    if (ImGui::BeginCombo("##provider", provider_preview)) {
        if (ImGui::Selectable("All providers", state.market_provider.empty()))
            state.market_provider.clear();
        for (const auto at : state.series_view.providers) {
            const auto& provider = rows[at].provider_code;
            if (hidden_provider(provider))
                continue;
            const bool chosen = provider == state.market_provider;
            if (ImGui::Selectable(provider.c_str(), chosen))
                state.market_provider = provider;
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The provider whose series this window lists.");

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (ImGui::Button("Cross rates")) {
        state.crm_picker_open = true;
        state.crm_picker_requested = false;
    }
    ImGui::SetItemTooltip(
        "Open a cross-rates matrix for this session's party.");

    if (state.crm_picker_open) {
        /* The popup sizes itself to what it holds, so its mark has a box. */
        constexpr ImVec2 popup_mark(220.0f, 90.0f);
        ImGui::OpenPopup("Choose CRM");
        if (ImGui::BeginPopup("Choose CRM")) {
            ImGui::TextUnformatted("Choose a CRM");
            ImGui::Separator();
            const auto* names = input.crm.names(
                input.sessions.instance_id(*session),
                input.sessions.account_id(*session),
                state.market_party_id);
            if (names == nullptr) {
                widget::draw_view_state_inline(
                    {.state = widget::view_state::reading}, popup_mark);
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
                widget::draw_view_state_inline(
                    {widget::view_state::no_data, widget::view_words::empty,
                        "No CRMs are configured for this party."},
                    popup_mark);
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

    if (catalogue_verdict.state != widget::view_state::none && rows.empty()) {
        widget::draw_view_state_body(status, foot, catalogue_verdict);
        draw_foot();
        ImGui::PopID();
        return;
    }

    /*
     * Filters that match nothing leave the whole body empty. The way out stays
     * a button, and the mark says nothing matched. The caller returns after
     * it, like every other whole-body state here.
     */
    const auto draw_no_match = [&](std::string_view what) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear filters"))
            std::fill(state.filter.begin(), state.filter.end(), '\0');
        widget::draw_view_state_body(status, foot,
            {widget::view_state::no_data, widget::view_words::empty, what});
        draw_foot();
        ImGui::PopID();
    };

    /*
     * The deployment's volatility surfaces. A surface is published under a
     * blueprint rather than as a series, so it has no row in the list below:
     * the chooser is its own control, and opening one draws it. It is offered
     * whatever the asset filter says, because a surface is not an asset class
     * and hiding it behind one would make it unfindable.
     */
    if (catalogue != nullptr && !catalogue->surfaces.empty()) {
        ImGui::AlignTextToFramePadding();
        widget::caption("Surface",
            "A volatility surface the deployment publishes.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.0f);
        const char* surface_preview = state.market_selected_surface_label.empty()
            ? "Select a volatility surface..."
            : state.market_selected_surface_label.c_str();
        if (ImGui::BeginCombo("##surface", surface_preview)) {
            for (const auto& surface : catalogue->surfaces) {
                const bool chosen
                    = surface.id == state.market_selected_surface_id;
                ImGui::PushID(surface.id.c_str());
                if (ImGui::Selectable(surface.name.c_str(), chosen)) {
                    state.market_selected_surface_id = surface.id;
                    state.market_selected_surface_label = surface.name;
                    request_vol_surface_window(input, *session, surface);
                }
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                if (!surface.smile_model.empty())
                    ImGui::SetItemTooltip("%s/%s \u00b7 %s model",
                        surface.base_currency.c_str(),
                        surface.quote_currency.c_str(),
                        surface.smile_model.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!catalogue->surfaces_error.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", catalogue->surfaces_error.c_str());
        }
    }

    if (model::domain::market_asset_of(state.market_asset_class)
        == model::domain::market_asset::ir) {
        const bool rebuilt = update_series_browser_view(state.series_view,
            {.generation = generation, .filter = filter,
                .asset_class = state.market_asset_class,
                .party = state.market_party_id,
                .provider = state.market_provider,
                .hidden_providers = hidden_providers},
            rows);
        const auto& curves = state.series_view.rows;
        /*
         * Said once per rebuild, so the party a list was built for is in the
         * log beside the rows it produced. A list of curves that seems to hold
         * another party's data is then a question about this line rather than
         * about the screen.
         */
        if (rebuilt)
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Curves listed for party '"
                << (state.market_party_id.empty() ? std::string{"any"}
                                                  : state.market_party_id)
                << "': " << curves.size() << " of " << rows.size()
                << " series, filter '" << filter << "'";
        /*
         * The deployment's own curves. A published curve is one calibrated
         * object rather than a tenor per series, so it is not a catalogue row:
         * the list carries it beside the tick curves, and opening it reads the
         * deployment's curve sets instead of assembling ticks.
         */
        const auto* read_catalogue = catalogue;
        const auto published = published_curves_matching(read_catalogue,
            state.market_party_id, filter);
        if (curves.empty() && published.empty()) {
            draw_no_match("No curves match the filters.");
            return;
        }

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
                const auto pair =
                    currencies_of_series(input, *session, curve.id, label);
                request_flags(input, *session, pair);
                const float label_end = draw_series_row(label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                ImGui::SetCursorScreenPos(ImVec2(label_end + 12.0f, row_y));
                /*
                 * The provider, in the combo as well as in the table: two
                 * curves of one currency can share a name - the same ladder
                 * quoted by two desks - and the provider is what tells them
                 * apart without opening either.
                 */
                ImGui::TextDisabled("\u00b7 %s", curve.provider_code.c_str());
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            for (const auto& curve : published) {
                ImGui::PushID(curve.series_id.c_str());
                if (ImGui::Selectable("##published-row"))
                    request_published_curve_window(input, *session, curve);
                const float row_y = ImGui::GetItemRectMin().y;
                const auto pair = currencies_of_series(input, *session,
                    curve.series_id, curve.label);
                request_flags(input, *session, pair);
                const float label_end = draw_series_row(curve.label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                ImGui::SetCursorScreenPos(ImVec2(label_end + 12.0f, row_y));
                ImGui::TextDisabled("%u pillars, published",
                    curve.points);
                ImGui::PopID();
            }
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
                const auto pair =
                    currencies_of_series(input, *session, curve.id, label);
                request_flags(input, *session, pair);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(curve.id.c_str());
                const bool chosen = curve.id
                    == state.market_selected_curve_key;
                ImGui::Selectable("##curve", chosen,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick);
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
            for (const auto& curve : published) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(curve.series_id.c_str());
                ImGui::Selectable("##published", false,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick);
                const auto pair = currencies_of_series(input, *session,
                    curve.series_id, curve.label);
                draw_series_row(curve.label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                if (ImGui::IsItemHovered()
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request_published_curve_window(input, *session, curve);
                ImGui::SetItemTooltip(
                    "%s, published by the deployment: %u pillars",
                    curve.series_id.c_str(), curve.points);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%u", curve.points);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("published");
            }
            ImGui::EndTable();
        }

        if (!published.empty())
            ImGui::TextDisabled(
                "Double-click a curve to open its term structure; a curve the "
                "deployment publishes is read as the deployment stated it.");
        else
            ImGui::TextDisabled(
                "Double-click a curve to open its term structure.");
        draw_foot();
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
            .party = state.market_party_id,
            .provider = state.market_provider,
            .hidden_providers = hidden_providers},
        rows);
    const auto& visible = state.series_view.rows;

    if (visible.empty()) {
        draw_no_match("No series match the filters.");
        return;
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
                const auto pair =
                    currencies_of_series(input, *session, row->id, row->label);
                request_flags(input, *session, pair);
                draw_series_row(row->label, 0.0f,
                    image_texture(input, *session, pair.base),
                    image_texture(input, *session, pair.quote));
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
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
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(row->id.c_str());
                ImGui::Selectable("##row", false,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick
                        | ImGuiSelectableFlags_AllowOverlap);
                const auto pair =
                    currencies_of_series(input, *session, row->id, row->label);
                request_flags(input, *session, pair);
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
    draw_foot();
    ImGui::PopID();
}

}
