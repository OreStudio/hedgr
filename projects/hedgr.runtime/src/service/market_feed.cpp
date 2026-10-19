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
#include "hedgr.runtime/service/market_feed.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.runtime/worker/market_stream.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;

using manager::connection_options_for;
using support::lg;
using support::live_windows;
using support::running_key;
using support::steady_now_ns;

}

market_feed::market_feed(const model::state::instance_table& instances,
    const model::state::session_table& sessions,
    const model::state::window_table& windows,
    model::state::market_table& market,
    const session::transport::session_options& seed,
    worker::tick_feed_factory feeds, catalogue_read read_catalogue,
    backfill_read read_backfill)
    : instances_(instances), sessions_(sessions), windows_(windows),
      market_(market), seed_(seed), market_feeds_(std::move(feeds)),
      read_catalogue_(std::move(read_catalogue)),
      read_backfill_(std::move(read_backfill)) {
    if (!market_feeds_) {
        market_feeds_ = [](worker::market_stream_options options)
            -> std::unique_ptr<worker::tick_feed> {
            auto stream =
                std::make_unique<worker::market_stream>(std::move(options));
            stream->start();
            return stream;
        };
    }
}

void market_feed::attach(std::filesystem::path database_path) {
    database_path_ = std::move(database_path);
}
void market_feed::open(const model::event::event& ev) {
    close(ev.instance_id, ev.account_id);
    if (!is_open())
        return;
    const auto instance = instances_.find(ev.instance_id);
    if (!instance)
        return;

    worker::market_stream_options options;
    options.options = connection_options_for(*instances_.get(*instance),
        seed_, database_path_);
    options.token = std::string{ev.token.view()};
    options.principal = ev.payload;
    options.tenant_id = ev.tenant_id;
    options.party_id = ev.party_id;

    auto stream = market_feeds_(std::move(options));

    market_stream_entry entry;
    entry.instance_id = ev.instance_id;
    entry.account_id = ev.account_id;
    entry.stream = std::move(stream);
    market_streams_.insert_or_assign(
        running_key(ev.instance_id, ev.account_id), std::move(entry));
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Market tick stream opened for '" << ev.instance_id << "' / '"
        << ev.account_id << "' in party '" << ev.party_id << "'";
}

void market_feed::retoken_or_open(const model::event::event& ev) {
    if (const auto stream = market_streams_.find(
            running_key(ev.instance_id, ev.account_id));
        stream != market_streams_.end() && stream->second.stream) {
        stream->second.stream->set_token(std::string{ev.token.view()});
    } else {
        open(ev);
    }
}

void market_feed::close(std::string_view instance_id,
    std::string_view account_id) {
    const auto key = running_key(instance_id, account_id);
    const auto found = market_streams_.find(key);
    if (found == market_streams_.end())
        return;
    retire_market_stream(std::move(found->second.stream));
    market_streams_.erase(found);
}

void market_feed::retire_market_stream(
    std::unique_ptr<worker::tick_feed> stream) {
    if (!stream)
        return;
    stream->request_stop();
    retired_market_streams_.push_back(std::move(stream));
}

std::vector<refusal> market_feed::pump() {
    std::vector<refusal> refusals;

    /*
     * What the open charts need, by session. A watch is only possible once the
     * catalogue names the series' URI, so a chart opened before the catalogue
     * read is watched on a later frame.
     */
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>> wanted;

    for (const auto window : live_windows(windows_)) {
        if (windows_.kind(window) != domain::window_kind::chart)
            continue;
        const auto session = windows_.session_of(window);
        if (!session)
            continue;
        const auto* chart =
            std::get_if<domain::chart_state>(&windows_.state(window));
        if (chart == nullptr || chart->series_id.empty())
            continue;

        const auto instance_id = sessions_.instance_id(*session);
        const auto account_id = sessions_.account_id(*session);
        const auto* catalogue =
            market_.catalogue(instance_id, account_id);
        if (catalogue == nullptr
            || catalogue->state
                == model_state::catalogue_state::failed) {
            /*
             * A restored chart has a series id and no catalogue to turn it into
             * the URI a subscription needs. Reading the catalogue is what the
             * series browser does; doing it here as well is what lets a chart
             * that outlived the process come back to life without one. A
             * failed read is left to the browser's cadence rather than retried
             * on every frame.
             */
            if (catalogue == nullptr) {
                if (auto refused = read_catalogue_(windows_.id(window)))
                    refusals.push_back(std::move(*refused));
            }
            continue;
        }

        const auto row = std::find_if(catalogue->series.begin(),
            catalogue->series.end(), [&chart](const auto& series) {
                return series.id == chart->series_id;
            });
        if (row == catalogue->series.end())
            continue;

        /*
         * The tick subject is party scoped and the token is for the session's
         * party. A chart looking at another party through the picker keeps its
         * backfill and manual refresh, not a live stream.
         */
        if (row->party_id != sessions_.party_id(*session))
            continue;

        wanted[running_key(instance_id, account_id)]
            .emplace_back(row->id, row->uri);
    }

    std::vector<model::state::market_bucket> buckets;
    const auto now = steady_now_ns();

    for (auto it = market_streams_.begin(); it != market_streams_.end();) {
        auto& entry = it->second;
        const auto session = sessions_.find(entry.instance_id,
            entry.account_id);
        if (!session
            || sessions_.status(*session)
                != domain::session_status::authenticated) {
            retire_market_stream(std::move(entry.stream));
            it = market_streams_.erase(it);
            continue;
        }

        const auto needed = wanted.find(it->first);
        std::unordered_map<std::string, std::string> desired;
        if (needed != wanted.end()) {
            for (const auto& [series_id, uri] : needed->second)
                desired.emplace(series_id, uri);
        }

        for (const auto& [series_id, uri] : desired) {
            if (entry.watched.contains(series_id))
                continue;
            entry.stream->watch(series_id, uri);
            entry.watched.emplace(series_id, uri);
            read_backfill_(entry.instance_id,
                entry.account_id, series_id);
        }
        for (auto watch = entry.watched.begin(); watch != entry.watched.end();) {
            if (desired.contains(watch->first)) {
                ++watch;
                continue;
            }
            entry.stream->unwatch(watch->first);
            watch = entry.watched.erase(watch);
        }

        if (entry.stream->take_reconnected()) {
            for (const auto& [series_id, uri] : entry.watched) {
                static_cast<void>(uri);
                read_backfill_(entry.instance_id,
                    entry.account_id, series_id);
            }
        }

        entry.stream->drain(buckets);
        for (const auto& bucket : buckets) {
            market_.append_point(entry.instance_id, entry.account_id,
                bucket.series_id, bucket.level, bucket.point, now);
        }

        ++it;
    }

    std::erase_if(retired_market_streams_,
        [](const auto& stream) { return stream->finished(); });

    return refusals;
}

void market_feed::cancel() {
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        retire_market_stream(std::move(entry.stream));
    }
    market_streams_.clear();
    retired_market_streams_.clear();
}

}
