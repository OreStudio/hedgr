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
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <format>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

using hedgr::model::command::event_queue;

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.market_worker");
    return instance;
}

[[nodiscard]] tick_silence& silence() {
    return market_silence();
}

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

boost::asio::awaitable<void> run_market(transport::session_link& link,
    market_job job, event_queue& events) {
    const auto key = job.key;

    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    auto fact = event::event{};
    fact.kind = job.catalogue ? event::event_kind::series_catalogue
        : job.history         ? event::event_kind::series_history
                              : event::event_kind::series_points;
    fact.history_from_ns = job.history_from_ns;
    fact.instance_id = key.instance_id;
    fact.account_id = key.account_id;
    fact.series_id = job.series_id;

    const bool tick_read = !job.catalogue;
    if (tick_read) {
        job.options.request_timeout = std::min(job.options.request_timeout,
            tick_read_timeout);
    }
    if (tick_read && silence().silent(key.instance_id, now_ns())) {
        fact.message = unanswered_text("not asked again for a few seconds");
        fact.observed_at_ns = now_ns();
        std::fill(job.token.begin(), job.token.end(), '\0');
        job.token.clear();
        job.principal.clear();
        const auto report = report_market_read(job, fact);
        BOOST_LOG_SEV(lg(), ores::logging::warn) << report.text;
        events.push(std::move(fact));
        co_return;
    }

    if (const auto connected = link.connect(); !connected) {
        fact.message = describe(connected.error());
    } else if (const auto resumed = link.resume(job.token, job.principal);
        !resumed) {
        fact.message = describe(resumed.error());
    } else if (job.catalogue) {
        if (const auto read = link.market_series(); read) {
            fact.series.reserve(read->size());
            for (const auto& row : *read) {
                fact.series.push_back(domain::market_series_row{
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
                fact.providers = std::move(*providers);
            } else {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The market provider read failed: "
                    << providers.error().message;
            }
        } else {
            fact.message = describe(read.error());
        }
    } else {
        const auto read = job.history
            ? link.market_series_history(job.series_id, job.history_from_ns,
                  job.history_to_ns, job.identity)
            : link.market_series_points(job.series_id, job.identity);
        if (read) {
            fact.history_truncated = read->truncated;
            fact.tick_from_ns = read->from_ns;
            fact.tick_to_ns = read->to_ns;
            fact.tick_coordinate = read->coordinate;
            fact.tick_coordinates = read->coordinates;
            fact.ticks_read = read->ticks;
            fact.points.reserve(read->points.size());
            for (const auto& point : read->points) {
                fact.points.push_back(domain::market_point{
                    .at_ns = point.at_ns,
                    .value = point.value});
            }
        } else {
            const bool unanswered = is_unanswered(read.error().code);
            silence().note(key.instance_id, unanswered, now_ns());
            fact.message = unanswered ? unanswered_text(describe(read.error()))
                                      : describe(read.error());
        }
        if (read)
            silence().note(key.instance_id, false, now_ns());
    }
    fact.observed_at_ns = now_ns();

    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    job.principal.clear();

    const auto report = report_market_read(job, fact);
    if (report.warn) {
        BOOST_LOG_SEV(lg(), ores::logging::warn) << report.text;
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::info) << report.text;
    }

    events.push(std::move(fact));
}

boost::asio::awaitable<void> run_market_with_client(market_job job,
    event_queue& events) {
    transport::session_client client(job.options);
    transport::session_client_link link(client);
    co_await run_market(link, std::move(job), events);
}

}

boost::asio::awaitable<void> run_market_read(transport::session_link& link,
    market_job job, event_queue& events) {
    return run_market(link, std::move(job), events);
}

tick_silence& market_silence() {
    static tick_silence instance;
    return instance;
}

market_read_report report_market_read(const market_job& job,
    const model::event::event& fact) {
    const auto& instance = fact.instance_id;
    const auto bounds = [&] {
        return fact.points.empty()
            ? std::string{}
            : std::format(", oldest_at_ns={} newest_at_ns={}",
                  fact.points.front().at_ns, fact.points.back().at_ns);
    };

    if (job.catalogue) {
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
                                   "of {} parties; by provider: {}",
            instance, fact.series.size(), parties.size(), providers)};
    }

    const auto kind = job.history ? "history" : "first load";
    const auto request = job.history
        ? std::format("from_ns={} to_ns={}", job.history_from_ns,
              job.history_to_ns)
        : std::format("first load, span widened until {} points",
              domain::market_first_load_min_points);
    if (!fact.message.empty()) {
        return {true, std::format("Market {} tick read for series '{}' on "
                                  "'{}' failed ({}): {}",
            kind, job.series_id, instance, request, fact.message)};
    }

    const auto answered = std::format("from_ns={} to_ns={}", fact.tick_from_ns,
        fact.tick_to_ns == 0 ? std::string{"now"}
                             : std::to_string(fact.tick_to_ns));
    if (fact.points.empty()) {
        return {true, std::format("Market {} for series '{}' on '{}' is empty "
                                  "({}): the deployment returned no ticks "
                                  "({} ticks of {} coordinates)",
            kind, job.series_id, instance, answered, fact.ticks_read,
            fact.tick_coordinates)};
    }
    const auto several = fact.tick_coordinates > 1;
    return {false,
        std::format("Market {} for series '{}' on '{}': {} ticks of '{}' "
                    "({}){}{}{}",
            kind, job.series_id, instance, fact.points.size(),
            fact.tick_coordinate, answered,
            fact.history_truncated
                ? ", the reply was at the ceiling or a retry shortened the span"
                : ", span read in full",
            several ? std::format(", plotted the first of {} coordinates",
                                  fact.tick_coordinates)
                    : std::string{},
            bounds())};
}

void spawn_market_read(boost::asio::io_context& context, market_job job,
    event_queue& events) {
    boost::asio::co_spawn(context,
        run_market_with_client(std::move(job), events),
        boost::asio::detached);
}

}
