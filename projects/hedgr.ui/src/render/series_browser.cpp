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
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/** @brief This file's logger: which instrument a row opened, and by what. */
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

/**
 * @brief The currencies an instrument names, for the flags beside its name.
 *
 * The deployment's own fields, not a reading of its name: a pair carries a base
 * and a quote, a rate carries the currency it is quoted in. A quote equal to the
 * base is not a pair and draws one flag rather than the same flag twice.
 */
[[nodiscard]] hedgr::ui::widget::currency_pair currencies_of_instrument(
    const domain::market_instrument& instrument) {
    hedgr::ui::widget::currency_pair pair;
    pair.base = instrument.base_currency.empty() ? instrument.currency
                                                 : instrument.base_currency;
    pair.quote = instrument.quote_currency;
    if (pair.quote == pair.base)
        pair.quote.clear();
    return pair;
}

/**
 * @brief The source an instrument is opened through, or nullptr.
 *
 * The first source with data that is not stale, because that is the price a
 * person expects to read; failing that any source with data, since a stale
 * price is still the instrument's own; failing that the first source at all, so
 * an instrument whose providers have never been observed still opens.
 */
[[nodiscard]] const domain::instrument_source* opening_source(
    const domain::market_instrument& instrument) {
    const domain::instrument_source* observed = nullptr;
    for (const auto& source : instrument.sources) {
        if (source.as_of_ns == 0)
            continue;
        if (!source.stale)
            return &source;
        if (observed == nullptr)
            observed = &source;
    }
    if (observed != nullptr)
        return observed;
    return instrument.sources.empty() ? nullptr : &instrument.sources.front();
}

/** @brief The row's flat icon that opens its mini plot, true when pressed. */
bool mini_plot_button(std::string_view what) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool pressed = ImGui::SmallButton(icon::dashboard);
    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("%s", what.data());
    return pressed;
}

/**
 * @brief The kind an instrument is, upper-cased for its badge.
 *
 * A stack buffer rather than a string: a badge is drawn for every visible row
 * on every frame, and a frame that allocates per row is a frame that stutters
 * with the scroll.
 */
[[nodiscard]] const char* kind_badge(std::string_view kind,
    std::array<char, 16>& buffer) {
    const auto count = std::min(kind.size(), buffer.size() - 1);
    for (std::size_t i = 0; i < count; ++i)
        buffer[i] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(kind[i])));
    buffer[count] = '\0';
    return buffer.data();
}

}

namespace hedgr::ui::render {

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

void request_published_curve_window(const render_input& input,
    hedgr::model::state::session_handle session,
    const domain::published_curve& curve) {
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

namespace {

/**
 * @brief Whether the deployment publishes the curve carried by @p series_id.
 *
 * A published curve is one object the deployment calibrates, with an instant
 * per state, rather than a series per rung. The catalogue carries published
 * curves beside the instruments, so whether a curve instrument is a published
 * one is a lookup rather than a guess.
 */
[[nodiscard]] bool published_under(
    const hedgr::model::state::series_catalogue* catalogue,
    std::string_view series_id) {
    if (catalogue == nullptr)
        return false;
    return std::any_of(catalogue->published_curves.begin(),
        catalogue->published_curves.end(), [&](const auto& curve) {
            return curve.series_id == series_id;
        });
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

/**
 * @brief Opens what one instrument row names, by the kind the deployment gave.
 *
 * The kind is the deployment's, so this is a dispatch and not a guess: a pair
 * opens its chart, a curve or a ladder opens the term structure, and a surface
 * opens the volatility surface published for its pair. A curve the deployment
 * publishes is opened as a published curve, because the deployment states its
 * points and its instants and assembling them from ticks would be inventing a
 * different object.
 */
void open_instrument(const render_input& input,
    hedgr::model::state::session_handle session,
    const domain::market_instrument& instrument) {
    const auto instance_id = std::string{input.sessions.instance_id(session)};
    const auto account_id = std::string{input.sessions.account_id(session)};
    const auto* catalogue = input.market.catalogue(instance_id, account_id);

    if (instrument.kind == "surface") {
        const auto pair = currencies_of_instrument(instrument);
        if (catalogue != nullptr) {
            for (const auto& surface : catalogue->surfaces) {
                if (surface.base_currency != pair.base
                    || surface.quote_currency != pair.quote)
                    continue;
                BOOST_LOG_SEV(lg(), ores::logging::info)
                    << "Surface instrument '" << instrument.name
                    << "' opens blueprint '" << surface.name << "'";
                request_vol_surface_window(input, session, surface);
                return;
            }
        }
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Surface instrument '" << instrument.name
            << "' has no blueprint for " << pair.base << "/" << pair.quote
            << "; nothing to open";
        return;
    }

    const auto* source = opening_source(instrument);
    if (source == nullptr) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Instrument '" << instrument.name << "' (" << instrument.kind
            << ") names no source series, so there is nothing to open";
        return;
    }

    if (instrument.kind == "pair") {
        request_series_window(input, session, domain::window_kind::chart,
            source->series_id, instrument.name);
        return;
    }

    if (published_under(catalogue, source->series_id)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Curve instrument '" << instrument.name
            << "' opens as the published curve under '" << source->series_id
            << "'";
        request_published_curve_window(input, session,
            domain::published_curve{.label = instrument.name,
                .series_id = source->series_id});
        return;
    }

    /*
     * A catalogue row carries the typed identity and the neighbouring rungs the
     * curve is gathered from. An instrument whose series the series list does
     * not carry - an older catalogue, or a series read that failed - still
     * opens: the window reads the one series the instrument named.
     */
    if (const auto* row = catalogue != nullptr
            ? catalogue->row_of(source->series_id)
            : nullptr) {
        open_curve(input, session, *row);
        return;
    }

    command request;
    request.kind = command_kind::open_window;
    request.new_window_kind = domain::window_kind::term_structure;
    request.instance_id = instance_id;
    request.account_id = account_id;
    request.series_id = source->series_id;
    request.series_label = instrument.name;
    request.curve_key = source->series_id;
    request.curve_label = instrument.name;
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Instrument '" << instrument.name << "' opens as the single series '"
        << source->series_id << "' (the catalogue does not list it)";
    input.commands.push(std::move(request));
}

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

    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputTextWithHint("##filter",
        "search: currency, pair, curve, provider",
        state.filter.data(), state.filter.size());
    ImGui::SameLine();
    if (ImGui::Button(icon::refresh))
        input.commands.push(command{
            .kind = command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    ImGui::SetItemTooltip("Read the instrument catalogue again");
    const auto* catalogue = input.market.catalogue(
        input.sessions.instance_id(*session),
        input.sessions.account_id(*session));
    static const std::vector<domain::market_instrument> none;
    const auto& instruments =
        catalogue != nullptr ? catalogue->instruments : none;

    /*
     * The providers the catalogue read brought with it. A reference provider
     * defines an instrument and never ticks, so hiding it is offered; an
     * instrument is hidden only when no visible provider is left behind it,
     * which the view decides.
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
    ImGui::TextDisabled("%zu instruments", instruments.size());

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
    catalogue_facts.reading = instruments.empty()
        && (catalogue_state == cat::catalogue_state::reading
            || catalogue_state == cat::catalogue_state::unread);
    catalogue_facts.read_failure =
        catalogue_state == cat::catalogue_state::failed
        ? std::string_view{catalogue->error} : std::string_view{};
    catalogue_facts.has_data = !instruments.empty();
    catalogue_facts.empty_what =
        "This deployment groups no market instruments for this session.";
    const auto catalogue_verdict = widget::view_state_of(catalogue_facts);
    trace_view_state("market data", "instruments", state, catalogue_verdict);
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
     * it.
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

    const bool rebuilt = update_series_browser_view(state.series_view,
        {.generation = generation, .filter = filter,
            .kind = state.market_kind,
            .provider = state.market_provider,
            .hidden_providers = hidden_providers},
        instruments);
    /*
     * Said once per rebuild, so the list in the log is the list on the screen:
     * a row that seems to belong to another party, or to be missing, is then a
     * question about this line rather than about the screen.
     */
    if (rebuilt)
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Instruments listed for party '"
            << (state.market_party_id.empty() ? std::string{"any"}
                                              : state.market_party_id)
            << "': " << state.series_view.rows.size() << " of "
            << instruments.size() << ", kind '"
            << (state.market_kind.empty() ? std::string{"any"}
                                          : state.market_kind)
            << "', filter '" << filter << "'";

    /*
     * The kind cut. It narrows and never changes what a row is: a curve stays a
     * curve row under every choice, which is the contradiction the old asset
     * combo created by switching the list from series to curves.
     */
    ImGui::SameLine();
    widget::caption("Kind", "The kind of market instrument to list.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* kind_preview =
        state.market_kind.empty() ? "All kinds" : state.market_kind.c_str();
    if (ImGui::BeginCombo("##kind", kind_preview)) {
        if (ImGui::Selectable("All kinds", state.market_kind.empty()))
            state.market_kind.clear();
        for (const auto& kind : state.series_view.kinds) {
            const bool chosen = kind == state.market_kind;
            if (ImGui::Selectable(kind.c_str(), chosen))
                state.market_kind = kind;
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    /*
     * The provider cut. One instrument may be quoted by several providers, and
     * only some of them tick, so this narrows the list to those with one.
     */
    if (std::find(state.series_view.providers.begin(),
            state.series_view.providers.end(), state.market_provider)
        == state.series_view.providers.end())
        state.market_provider.clear();
    ImGui::SameLine();
    widget::caption("Providers", "The provider whose instruments are listed.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* provider_preview = state.market_provider.empty()
        ? "All providers" : state.market_provider.c_str();
    if (ImGui::BeginCombo("##provider", provider_preview)) {
        if (ImGui::Selectable("All providers", state.market_provider.empty()))
            state.market_provider.clear();
        for (const auto& provider : state.series_view.providers) {
            if (std::find(hidden_providers.begin(), hidden_providers.end(),
                    provider)
                != hidden_providers.end())
                continue;
            const bool chosen = provider == state.market_provider;
            if (ImGui::Selectable(provider.c_str(), chosen))
                state.market_provider = provider;
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The provider whose instruments this window lists.");

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

    if (catalogue_verdict.state != widget::view_state::none
        && instruments.empty()) {
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
        if (ImGui::SmallButton("Clear filters")) {
            std::fill(state.filter.begin(), state.filter.end(), '\0');
            state.market_kind.clear();
            state.market_provider.clear();
        }
        widget::draw_view_state_body(status, foot,
            {widget::view_state::no_data, widget::view_words::empty, what});
        draw_foot();
        ImGui::PopID();
    };

    /*
     * The deployment's volatility surfaces. A surface is published under a
     * blueprint rather than as a series, and the catalogue does not yet report
     * one as an instrument, so the chooser is its own control. It is offered
     * whatever the facets say, because hiding a surface behind one would make
     * it unfindable.
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

    const auto& visible = state.series_view.rows;
    if (visible.empty()) {
        draw_no_match("No instruments match the filters.");
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_ScrollY;

    const float height = std::max(ImGui::GetContentRegionAvail().y
            - widget::status_foot_height(status)
            - ImGui::GetFrameHeightWithSpacing(),
        120.0f);
    if (ImGui::BeginTable("##instruments", 6, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed,
            74.0f);
        ImGui::TableSetupColumn("Instrument",
            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Providers", ImGuiTableColumnFlags_WidthFixed,
            130.0f);
        ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthFixed,
            120.0f);
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 70.0f);
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
                const auto* instrument =
                    &instruments[visible[static_cast<std::size_t>(index)]];
                const auto pair = currencies_of_instrument(*instrument);
                const auto* source = opening_source(*instrument);
                /*
                 * The flags are looked up for every visible row, so the pair is
                 * read from the instrument's own fields rather than parsed from
                 * a label: no allocation on the frame path, and no reading of a
                 * name that could disagree with the deployment. Asking for the
                 * image is what makes the next frame able to draw it.
                 */
                request_flags(input, *session, pair);
                const auto base_texture =
                    image_texture(input, *session, pair.base);
                const auto quote_texture =
                    image_texture(input, *session, pair.quote);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(instrument->instrument_id.c_str());
                std::array<char, 16> badge{};
                ImGui::TextDisabled("%s", kind_badge(instrument->kind, badge));
                ImGui::TableNextColumn();
                ImGui::Selectable("##instrument", false,
                    ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick
                        | ImGuiSelectableFlags_AllowOverlap);
                draw_series_row(instrument->name, 0.0f, base_texture,
                    quote_texture);
                if (ImGui::IsItemHovered()
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    open_instrument(input, *session, *instrument);
                if (instrument->stale)
                    ImGui::SetItemTooltip(
                        "%s\n%s \u00b7 %s\nEvery source behind it is stale.",
                        instrument->instrument_id.c_str(),
                        instrument->instrument_type.c_str(),
                        instrument->quote_type.c_str());
                else
                    ImGui::SetItemTooltip("%s\n%s \u00b7 %s",
                        instrument->instrument_id.c_str(),
                        instrument->instrument_type.c_str(),
                        instrument->quote_type.c_str());

                ImGui::TableNextColumn();
                for (std::size_t i = 0; i < instrument->sources.size(); ++i)
                    ImGui::TextDisabled("%s%s", i == 0 ? "" : ", ",
                        instrument->sources[i].provider_code.c_str());

                ImGui::TableNextColumn();
                if (instrument->axes.empty())
                    ImGui::TextDisabled("%s", "-");
                for (std::size_t i = 0; i < instrument->axes.size(); ++i) {
                    const auto& axis = instrument->axes[i];
                    ImGui::TextDisabled("%s%zu %s", i == 0 ? "" : " \u00b7 ",
                        axis.values.size(), axis.field.c_str());
                }

                ImGui::TableNextColumn();
                /*
                 * The age is the deployment's own count for the source the row
                 * opens through, not this client's subtraction of two clocks.
                 */
                if (instrument->stale)
                    ImGui::TextDisabled("stale");
                else if (source != nullptr && source->age_seconds != 0)
                    ImGui::TextDisabled("%llds",
                        static_cast<long long>(source->age_seconds));
                else
                    ImGui::TextDisabled("%s", "-");

                ImGui::TableNextColumn();
                if (source != nullptr && mini_plot_button("Open a mini plot"))
                    request_series_window(input, *session,
                        domain::window_kind::mini_plot, source->series_id,
                        instrument->name,
                        chart_span_ns(input.market_view.chart_span));
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled(
        "Double-click an instrument to open it: a curve opens its term "
        "structure, a pair its chart, a surface its volatility.");
    draw_foot();
    ImGui::PopID();
}

}
