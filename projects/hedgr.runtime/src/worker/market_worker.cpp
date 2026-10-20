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
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
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

boost::asio::awaitable<void> run_market(market_job job,
    event_queue& events) {
    const auto key = job.key;

    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    auto fact = event::event{};
    fact.kind = job.catalogue ? event::event_kind::series_catalogue
                              : event::event_kind::series_points;
    fact.instance_id = key.instance_id;
    fact.account_id = key.account_id;
    fact.series_id = job.series_id;

    transport::session_client client(job.options);
    if (const auto connected = client.connect(); !connected) {
        fact.message = describe(connected.error());
    } else if (const auto resumed = client.resume(job.token, job.principal);
        !resumed) {
        fact.message = describe(resumed.error());
    } else if (job.catalogue) {
        if (const auto read = client.market_series(); read) {
            fact.series.reserve(read->size());
            for (const auto& row : *read) {
                fact.series.push_back(domain::market_series_row{
                    .id = row.id,
                    .uri = row.oresmd_uri,
                    .label = row.label,
                    .asset_class = row.asset_class,
                    .producer_kind = row.producer_kind,
                    .party_id = row.party_id,
                    .tenor = row.tenor,
                    .curve_key = row.curve_key,
                    .curve_label = row.curve_label});
            }
        } else {
            fact.message = describe(read.error());
        }
    } else {
        if (const auto read = client.market_series_points(job.series_id,
                job.limit);
            read) {
            fact.points.reserve(read->points.size());
            for (const auto& point : read->points) {
                fact.points.push_back(domain::market_point{
                    .at_ns = point.at_ns,
                    .value = point.value});
            }
        } else {
            fact.message = describe(read.error());
        }
    }
    fact.observed_at_ns = now_ns();

    std::fill(job.token.begin(), job.token.end(), '\0');
    job.token.clear();
    job.principal.clear();

    if (!fact.message.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Market read for '" << key.instance_id << "' failed: "
            << fact.message;
    } else if (job.catalogue) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Market catalogue for '" << key.instance_id << "' holds "
            << fact.series.size() << " series";
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::debug)
            << "Market points for series '" << job.series_id << "': "
            << fact.points.size() << " points";
    }

    events.push(std::move(fact));
}

}

void spawn_market_read(boost::asio::io_context& context, market_job job,
    event_queue& events) {
    boost::asio::co_spawn(context, run_market(std::move(job), events),
        boost::asio::detached);
}

}
