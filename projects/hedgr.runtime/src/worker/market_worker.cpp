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
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/tick_silence.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.market_worker");
    return instance;
}

[[nodiscard]] tick_silence& silence() {
    return market_silence();
}

/**
 * @brief How many published curves a catalogue read lists.
 *
 * The chooser lists the deployment's curves, a couple of dozen on the desk it
 * was built against, so this is a generous ceiling rather than a page size.
 */
constexpr std::uint32_t curve_heads_limit = 200;

/**
 * @brief How many surface blueprints a catalogue read lists.
 *
 * A deployment publishes one blueprint per pair and model, so this is the same
 * generous ceiling the curves use rather than a page size.
 */
constexpr std::uint32_t surface_blueprint_limit = 200;

/** @brief What an unanswered tick read says, for the chart's foot and plot. */
[[nodiscard]] std::string unanswered_text(const std::string& detail) {
    return "The marketdata service did not answer the tick history call "
        "(get_series_ticks). It may need a restart to load it"
        + (detail.empty() ? std::string{} : " (" + detail + ")") + ".";
}

[[nodiscard]] std::string describe(const transport::session_error& error) {
    return error.message.empty()
        ? std::string{transport::to_string(error.code)}
        : error.message;
}

[[nodiscard]] std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/** @brief The series a tick request names, or empty for the catalogue. */
[[nodiscard]] std::string_view series_of(const read_request& request) {
    if (const auto* points = std::get_if<market_points_request>(&request))
        return points->series_id;
    if (const auto* history = std::get_if<market_history_request>(&request))
        return history->series_id;
    return {};
}

}

market_read_report report_market_read(const read_request& request,
    const model::event::event& fact) {
    const auto& instance = fact.instance_id;

    if (std::holds_alternative<catalogue_request>(request)) {
        if (!fact.message.empty()) {
            return {true, std::format("Market catalogue read for '{}' "
                                      "failed: {}", instance, fact.message)};
        }
        std::map<std::string, std::size_t> by_provider;
        std::set<std::string> parties;
        for (const auto& row : fact.series) {
            ++by_provider[row.provider_code];
            parties.insert(row.party_id);
        }
        std::string providers;
        for (const auto& [code, count] : by_provider) {
            providers += std::format("{}{} '{}'", providers.empty() ? "" : ", ",
                count, code);
        }
        return {false, std::format("Market catalogue for '{}' holds {} series "
                                   "of {} parties, {} instruments and {} "
                                   "published curves; by provider: {}",
            instance, fact.series.size(), parties.size(),
            fact.instruments.size(), fact.published_curves.size(), providers)};
    }

    const auto* history = std::get_if<market_history_request>(&request);
    const auto series_id = std::string{series_of(request)};
    const auto kind = history != nullptr ? "history" : "first load";
    const auto asked = history != nullptr
        ? std::format("from_ns={} to_ns={}", history->from_ns, history->to_ns)
        : std::format("first load, span widened until {} points",
              domain::market_first_load_min_points);
    const auto bounds = [&] {
        return fact.points.empty()
            ? std::string{}
            : std::format(", oldest_at_ns={} newest_at_ns={}",
                  fact.points.front().at_ns, fact.points.back().at_ns);
    };

    if (!fact.message.empty()) {
        return {true, std::format("Market {} tick read for series '{}' on "
                                  "'{}' failed ({}): {}",
            kind, series_id, instance, asked, fact.message)};
    }

    const auto answered = std::format("from_ns={} to_ns={}", fact.tick_from_ns,
        fact.tick_to_ns == 0 ? std::string{"now"}
                             : std::to_string(fact.tick_to_ns));
    if (fact.points.empty()) {
        return {true, std::format("Market {} for series '{}' on '{}' is empty "
                                  "({}): the deployment returned no ticks "
                                  "({} ticks of {} coordinates)",
            kind, series_id, instance, answered, fact.ticks_read,
            fact.tick_coordinates)};
    }
    const auto several = fact.tick_coordinates > 1;
    return {false,
        std::format("Market {} for series '{}' on '{}': {} ticks of '{}' "
                    "({}){}{}{}",
            kind, series_id, instance, fact.points.size(), fact.tick_coordinate,
            answered,
            fact.history_truncated
                ? ", the reply was at the ceiling or a retry shortened the span"
                : ", span read in full",
            several ? std::format(", plotted the first of {} coordinates",
                          fact.tick_coordinates)
                    : std::string{},
            bounds())};
}

void run_market_read(transport::session_link& link, const read_request& request,
    event::event& answer) {
    const auto instance = answer.instance_id;
    const bool catalogue = std::holds_alternative<catalogue_request>(request);

    /*
     * A ladder is one object and one read, and it has nothing to do with the
     * tick silence a chart's read observes: a ladder that fails is a fact the
     * window shows, not a series that has gone quiet.
     */
    if (const auto* ladder = std::get_if<rate_ladder_request>(&request)) {
        if (const auto read = link.rate_ladder(ladder->currency,
                ladder->instrument_type, ladder->forward_start, {}, 0);
            read) {
            answer.ladder = *read;
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Rate ladder for " << ladder->currency << " "
                << ladder->instrument_type << " on '" << instance << "': "
                << read->rungs.size() << " rungs from "
                << read->provider_count << " providers";
        } else {
            answer.message = describe(read.error());
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The rate ladder read for '" << ladder->currency << " "
                << ladder->instrument_type << "' failed: " << answer.message;
        }
        answer.observed_at_ns = now_ns();
        return;
    }

    if (!catalogue && silence().silent(instance, now_ns())) {
        answer.message = unanswered_text("not asked again for a few seconds");
        answer.observed_at_ns = now_ns();
        const auto report = report_market_read(request, answer);
        BOOST_LOG_SEV(lg(), ores::logging::warn) << report.text;
        return;
    }

    if (catalogue) {
        if (const auto read = link.market_series(); read) {
            answer.series.reserve(read->size());
            for (const auto& row : *read) {
                answer.series.push_back(domain::market_series_row{
                    .id = row.id,
                    .uri = row.oresmd_uri,
                    .label = row.label,
                    .asset_class = row.asset_class,
                    .provider_code = row.provider_code,
                    .party_id = row.party_id,
                    .tenor = row.tenor,
                    .curve_key = row.curve_key,
                    .curve_label = row.curve_label,
                    .identity = row.identity,
                    .asset = row.asset});
            }
            /*
             * The providers travel with the catalogue because the browser
             * needs them to tell a reference series from a data vendor's. A
             * failed provider read leaves the list empty and hides nothing;
             * it does not fail the catalogue.
             */
            if (const auto providers = link.list_providers(); providers) {
                answer.providers = std::move(*providers);
            } else {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The market provider read failed: "
                    << providers.error().message;
            }

            /*
             * The curves the deployment publishes travel with the catalogue
             * for the same reason the providers do: the browser is what opens
             * a curve, and one refresh fills both of its lists. A failed curve
             * read leaves its list empty and says why; it does not fail the
             * catalogue.
             */
            /*
             * The surface blueprints travel with the catalogue for the curves'
             * reason: the browser is what opens a surface. A deployment that
             * publishes none answers with an empty list, which is a fact about
             * the desk rather than a failure, so it is not logged as one.
             */
            if (const auto surfaces
                = link.fx_vol_surface_blueprints(surface_blueprint_limit);
                surfaces) {
                answer.surface_blueprints.reserve(surfaces->size());
                for (const auto& blueprint : *surfaces) {
                    answer.surface_blueprints.push_back(
                        hedgr::model::domain::surface_blueprint{
                            .id = blueprint.id,
                            .name = blueprint.name,
                            .base_currency = blueprint.base_currency,
                            .quote_currency = blueprint.quote_currency,
                            .smile_model = blueprint.smile_model,
                            .expiries = blueprint.expiries,
                            .strikes = blueprint.strikes});
                }
            } else {
                answer.surface_blueprints_error
                    = surfaces.error().message.empty()
                    ? surfaces.error().detail : surfaces.error().message;
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The surface blueprint read failed: "
                    << answer.surface_blueprints_error;
            }

            if (const auto curves = link.published_curves(curve_heads_limit);
                curves) {
                answer.published_curves = std::move(*curves);
            } else {
                answer.published_curves_error = curves.error().message.empty()
                    ? curves.error().detail : curves.error().message;
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The published curve read failed: "
                    << answer.published_curves_error;
            }

            /*
             * The instruments the deployment groups those series into: the
             * rows the market window draws. Read whole rather than filtered,
             * because the window shows the whole catalogue and narrows it in
             * place; a failed read leaves the list empty and says why, which is
             * a different thing from a deployment that holds none.
             */
            if (auto instruments = link.list_instruments({}); instruments) {
                answer.instruments = std::move(*instruments);
            } else {
                answer.instruments_error = describe(instruments.error());
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The instrument read failed: " << answer.instruments_error;
            }
        } else {
            answer.message = describe(read.error());
        }
    } else {
        const auto* history = std::get_if<market_history_request>(&request);
        const auto* points = std::get_if<market_points_request>(&request);
        const auto read = history != nullptr
            ? link.market_series_history(history->series_id, history->from_ns,
                  history->to_ns, history->identity, tick_read_timeout)
            : link.market_series_points(points->series_id, points->identity,
                  tick_read_timeout);
        if (read) {
            answer.history_truncated = read->truncated;
            answer.tick_from_ns = read->from_ns;
            answer.tick_to_ns = read->to_ns;
            answer.tick_coordinate = read->coordinate;
            answer.tick_coordinates = read->coordinates;
            answer.ticks_read = read->ticks;
            answer.points.reserve(read->points.size());
            for (const auto& point : read->points) {
                answer.points.push_back(domain::market_point{
                    .at_ns = point.at_ns, .value = point.value});
            }
        } else {
            const bool unanswered = is_unanswered(read.error().code);
            silence().note(instance, unanswered, now_ns());
            answer.message = unanswered
                ? unanswered_text(describe(read.error()))
                : describe(read.error());
        }
        if (read)
            silence().note(instance, false, now_ns());
    }
    answer.observed_at_ns = now_ns();

    const auto report = report_market_read(request, answer);
    if (report.warn) {
        BOOST_LOG_SEV(lg(), ores::logging::warn) << report.text;
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::info) << report.text;
    }
}

tick_silence& market_silence() {
    static tick_silence instance;
    return instance;
}

}
