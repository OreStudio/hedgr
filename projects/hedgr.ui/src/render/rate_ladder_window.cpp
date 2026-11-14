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
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/series_text.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <format>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace theme = hedgr::ui::theme;

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/** @brief This file's logger: which ladder a window read, and from what. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.rate_ladder");
    return instance;
}

/**
 * @brief The provider columns one ladder needs, in the deployment's order.
 *
 * Taken from the quotes the deployment actually sent rather than from the
 * provider list, so a provider that quotes nothing here gets no empty column.
 * The order is the order the rungs named them, which is the deployment's.
 */
[[nodiscard]] std::vector<std::string> providers_of(
    const domain::rate_ladder& ladder) {
    std::vector<std::string> providers;
    for (const auto& rung : ladder.rungs) {
        for (const auto& quote : rung.quotes) {
            if (std::find(providers.begin(), providers.end(),
                    quote.provider_code)
                == providers.end())
                providers.push_back(quote.provider_code);
        }
    }
    return providers;
}

/**
 * @brief One provider's newest quote of one rung, or nullptr.
 *
 * The design states a provider's cell is the newest value it gave for the rung.
 * A deployment that answers with one quote per provider makes this a plain
 * lookup; one that answers with a provider's recent observations - which
 * prime_origin does today - makes it the newest of them, and
 * @c observations says how many there were so the cell does not hide the rest
 * without saying so.
 */
struct cell final {
    const domain::ladder_quote* quote = nullptr;
    std::size_t observations = 0;
};

[[nodiscard]] cell newest_quote(const domain::ladder_rung& rung,
    std::string_view provider) {
    cell found;
    for (const auto& quote : rung.quotes) {
        if (quote.provider_code != provider)
            continue;
        ++found.observations;
        if (found.quote == nullptr || quote.as_of_ns > found.quote->as_of_ns)
            found.quote = &quote;
    }
    return found;
}

/*
 * The windows a ladder window names itself by. Choosing a ladder is a read, not
 * a filter over something already held, so the choice goes through the runtime:
 * it writes the window's state and then reads, and the next refresh reads the
 * same ladder rather than the one it replaced.
 */

/** @brief Points this window at @p currency and reads it. */
void request_ladder(const render_input& input, std::string_view window_id,
    const domain::rate_ladder_state& ladder, std::string_view currency,
    std::string_view instrument_type) {
    command request;
    request.kind = command_kind::configure_rate_ladder;
    request.window_id = std::string{window_id};
    request.currency = std::string{currency};
    request.instrument_type = std::string{instrument_type};
    request.forward_start = ladder.forward_start;
    request.curve_label = currency.empty()
        ? std::string{}
        : std::format("{} {}", currency,
              instrument_type.empty() ? "rates" : instrument_type);
    input.commands.push(std::move(request));
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Rates window asks for " << request.currency << " "
        << request.instrument_type << " (was " << ladder.currency << " "
        << ladder.instrument_type << ")";
}

}

namespace hedgr::ui::render {

void render_rate_ladder(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status, lock_control& lock) {
    static_cast<void>(lock);

    ImGui::PushID("rate-ladder");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* window = std::get_if<domain::rate_ladder_state>(&shape);
    if (window == nullptr) {
        widget::draw_view_state_body(status, foot,
            {widget::view_state::error, widget::view_words::read_failed,
                "This window does not hold a rate ladder."});
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
    const auto key = hedgr::model::state::ladder_key(window->currency,
        window->instrument_type, window->forward_start);
    const auto* row = input.ladders.get(instance_id, account_id, key);
    const auto* ladder = row != nullptr ? &row->ladder : nullptr;
    const bool reading = row != nullptr
        && row->state == model::state::ladder_state::reading;
    const bool has_rungs = ladder != nullptr && !ladder->rungs.empty();

    auto facts = session_view_facts(input, handle);
    facts.reading = row == nullptr || reading;
    facts.read_failure = row != nullptr ? std::string_view{row->error}
                                        : std::string_view{};
    facts.has_data = has_rungs;
    facts.empty_what = window->currency.empty()
        ? "Choose a currency to read its ladder."
        : "This deployment quotes no ladder for the ladder this window names.";
    const auto verdict = widget::view_state_of(facts);
    trace_view_state("rate ladder", window->label, state, verdict);

    if (ladder != nullptr) {
        set_health_foot(foot,
            hedgr::model::domain::assess_data_health(ladder->as_of_ns,
                input.wall_now_ns,
                hedgr::model::domain::ir_health_thresholds),
            ladder->as_of_ns, input.wall_now_ns, reading);
    }
    widget::set_view_foot(foot, verdict);

    /*
     * The ladder the window shows, and the two controls that change it. A desk
     * reaches a ladder in two clicks - a currency and an instrument - and both
     * are read from the instruments the catalogue already holds rather than
     * typed.
     */
    ImGui::AlignTextToFramePadding();
    widget::value_pill(window->label.empty() ? std::string_view{"Rates"}
                                             : std::string_view{window->label});
    ImGui::SameLine();
    widget::caption("Currency", "The currency the ladder is quoted in.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    const char* currency_preview = window->currency.empty()
        ? "..." : window->currency.c_str();
    static const std::vector<domain::market_instrument> no_instruments;
    const auto* catalogue = input.market.catalogue(instance_id, account_id);
    const auto& instruments =
        catalogue != nullptr ? catalogue->instruments : no_instruments;
    if (ImGui::BeginCombo("##currency", currency_preview)) {
        std::vector<std::string> currencies;
        for (const auto& instrument : instruments) {
            const auto& currency = instrument.base_currency.empty()
                ? instrument.currency : instrument.base_currency;
            if (currency.size() != 3)
                continue;
            if (std::find(currencies.begin(), currencies.end(), currency)
                == currencies.end())
                currencies.push_back(currency);
        }
        std::sort(currencies.begin(), currencies.end());
        for (const auto& currency : currencies) {
            const bool chosen = currency == window->currency;
            if (ImGui::Selectable(currency.c_str(), chosen) && !chosen)
                request_ladder(input, input.windows.id(handle), *window, currency,
                    window->instrument_type);
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    widget::caption("Instrument", "The instrument the ladder quotes.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* instrument_preview = window->instrument_type.empty()
        ? "..." : window->instrument_type.c_str();
    if (ImGui::BeginCombo("##instrument", instrument_preview)) {
        std::vector<std::string> types;
        for (const auto& instrument : instruments) {
            if (instrument.instrument_type.empty())
                continue;
            if (std::find(types.begin(), types.end(),
                    instrument.instrument_type)
                == types.end())
                types.push_back(instrument.instrument_type);
        }
        std::sort(types.begin(), types.end());
        for (const auto& type : types) {
            const bool chosen = type == window->instrument_type;
            if (ImGui::Selectable(type.c_str(), chosen) && !chosen)
                request_ladder(input, input.windows.id(handle), *window,
                    window->currency, type);
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    /*
     * The conventions the numbers rest on, stated on the screen: a rate without
     * its day count is a number two people can read two ways, and these are the
     * deployment's words rather than an assumption this client made.
     */
    if (has_rungs) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Conventions",
            "The conventions the numbers rest on, as the deployment states "
            "them.");
        ImGui::SameLine();
        ImGui::TextDisabled("%s \u00b7 %s \u00b7 %d provider%s",
            ladder->tenor_convention.empty() ? "-" : ladder->tenor_convention.c_str(),
            ladder->day_count_convention.empty()
                ? "-" : ladder->day_count_convention.c_str(),
            ladder->provider_count,
            ladder->provider_count == 1 ? "" : "s");
        if (!window->forward_start.empty()) {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
            widget::caption("Forward start", "Where the ladder starts.");
            ImGui::SameLine();
            ImGui::TextUnformatted(window->forward_start.c_str());
        }
    }

    const float foot_height = widget::status_foot_height(status, foot);
    if (verdict.state != widget::view_state::none && !has_rungs) {
        widget::draw_view_state_body(status, foot, verdict);
        static_cast<void>(
            draw_foot_with_party(input, *session, status, foot, state));
        ImGui::PopID();
        return;
    }

    const auto providers = providers_of(*ladder);
    /* Rung, maturity, year, one per provider, derived, age. */
    const auto columns = static_cast<int>(providers.size()) + 5;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_ScrollY;
    const float height = std::max(ImGui::GetContentRegionAvail().y
            - foot_height - ImGui::GetStyle().ItemSpacing.y,
        120.0f);
    if (ImGui::BeginTable("##ladder", columns, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Rung", ImGuiTableColumnFlags_WidthFixed,
            60.0f);
        ImGui::TableSetupColumn("Maturity", ImGuiTableColumnFlags_WidthFixed,
            92.0f);
        ImGui::TableSetupColumn("Year", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        for (const auto& provider : providers)
            ImGui::TableSetupColumn(provider.c_str(),
                ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Derived", ImGuiTableColumnFlags_WidthFixed,
            120.0f);
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const auto& rung : ladder->rungs) {
            ImGui::TableNextRow();
            ImGui::PushID(rung.tenor.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(rung.tenor.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", rung.maturity.c_str());
            ImGui::TableNextColumn();
            const auto years = format_number("{:.4f}", rung.year_fraction);
            ImGui::TextDisabled("%s", years.c_str());

            /*
             * A provider that quoted the rung shows its newest value; one that
             * did not shows a gap, because a zero would read as a rate. The
             * count beside a value says how many observations it stands on, so
             * a deployment answering with a provider's history says so rather
             * than hiding it.
             */
            for (const auto& provider : providers) {
                ImGui::TableNextColumn();
                const auto found = newest_quote(rung, provider);
                if (found.quote == nullptr) {
                    ImGui::TextDisabled("-");
                    continue;
                }
                const auto value = format_price(found.quote->value);
                ImGui::TextUnformatted(value.c_str());
                if (found.observations > 1) {
                    ImGui::SameLine(0.0f, 2.0f);
                    ImGui::TextDisabled("\u00b7%zu", found.observations);
                    ImGui::SetItemTooltip(
                        "The newest of %zu observations this provider gave for "
                        "the rung.", found.observations);
                }
                if (found.quote->stale) {
                    ImGui::SameLine(0.0f, 2.0f);
                    ImGui::TextDisabled("stale");
                }
            }

            ImGui::TableNextColumn();
            const auto derived = format_price(rung.consensus.value);
            ImGui::TextUnformatted(derived.c_str());
            if (!rung.consensus.method.empty()) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("(%s, %d)", rung.consensus.method.c_str(),
                    rung.consensus.providers_used);
            }

            ImGui::TableNextColumn();
            /*
             * The age is the deployment's own count for the newest quote in the
             * row, not this client's subtraction of two clocks.
             */
            std::int64_t age = -1;
            for (const auto& quote : rung.quotes)
                if (age < 0 || quote.age_seconds < age)
                    age = quote.age_seconds;
            if (age < 0)
                ImGui::TextDisabled("-");
            else
                ImGui::TextDisabled("%llds", static_cast<long long>(age));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled(
        "A rung's provider cell is the newest value that provider gave it; the "
        "derived column is the deployment's own. Staleness is the deployment's "
        "verdict.");

    static_cast<void>(
        draw_foot_with_party(input, *session, status, foot, state));
    ImGui::PopID();
}

}
