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
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <string>
#include <utility>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;

using manager::connection_options_for;
using support::lg;
using support::running_key;
using support::steady_now_ns;

}

market_feed::market_feed(const model::state::instance_table& instances,
    const model::state::session_table& sessions,
    model::state::series_table& series,
    const session::transport::session_options& seed,
    worker::tick_feed_factory feeds, catalogue_read read_catalogue,
    backfill_read read_backfill)
    : instances_(instances), sessions_(sessions), series_(series),
      seed_(seed), market_feeds_(std::move(feeds)),
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
    if (const auto session = sessions_.find(ev.instance_id, ev.account_id))
        entry.session = *session;
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

bool market_feed::wanted(const market_stream_entry& entry,
    model::state::series_handle series, std::vector<refusal>& refusals) {
    if (series_.watchers(series) == 0)
        return false;

    /*
     * A watch is only possible once the catalogue names the series' URI. A
     * restored chart has a series id and no catalogue to turn it into one, so
     * the feed asks for the catalogue itself, which is what lets a chart that
     * outlived the process come back to life without the series browser. The
     * rows are refreshed when it lands, and that puts the chart back on the
     * change list.
     */
    if (series_.catalogue(entry.instance_id, entry.account_id) == nullptr) {
        if (auto refused =
                read_catalogue_(entry.instance_id, entry.account_id))
            refusals.push_back(std::move(*refused));
        return false;
    }

    const auto* info = series_.info(series);
    if (info == nullptr || !info->listed)
        return false;

    /*
     * The tick subject is party scoped and the token is for the session's
     * party. A chart looking at another party through the picker keeps its
     * backfill and manual refresh, not a live stream.
     */
    const auto session = sessions_.find(entry.instance_id, entry.account_id);
    if (!session)
        return false;
    if (info->party_id != sessions_.party_id(*session)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Not watching series '" << series_.key_of(series)->series_id
            << "': it belongs to party '" << info->party_id
            << "' and the session is party '" << sessions_.party_id(*session)
            << "'";
        return false;
    }
    return true;
}

void market_feed::reconcile(market_stream_entry& entry,
    model::state::series_handle series, std::vector<refusal>& refusals) {
    const auto watching = std::ranges::find(entry.watched, series);
    const bool is_watched = watching != entry.watched.end();
    const bool is_wanted = wanted(entry, series, refusals);
    if (is_wanted == is_watched)
        return;

    if (!is_wanted) {
        entry.stream->unwatch(series);
        entry.watched.erase(watching);
        return;
    }

    entry.watched.push_back(series);
    const auto key = *series_.key_of(series);
    if (auto canonical = session::market::canonical_series_uri(
            series_.info(series)->uri)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Watching series '" << key.series_id << "' as '" << *canonical
            << "'";
        entry.stream->watch(series, std::move(*canonical));
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Not watching series '" << key.series_id << "' in party '"
            << series_.info(series)->party_id << "': its URI names no series: "
            << series_.info(series)->uri;
    }
    read_backfill_(key.instance_id, key.account_id, key.series_id);
}

void market_feed::forget(model::state::series_handle series) {
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        const auto watching = std::ranges::find(entry.watched, series);
        if (watching == entry.watched.end())
            continue;
        entry.stream->unwatch(series);
        entry.watched.erase(watching);
    }
}

std::vector<refusal> market_feed::pump() {
    std::vector<refusal> refusals;

    for (auto it = market_streams_.begin(); it != market_streams_.end();) {
        const auto session = it->second.session;
        if (sessions_.contains(session)
            && sessions_.status(session)
                == domain::session_status::authenticated) {
            ++it;
            continue;
        }
        retire_market_stream(std::move(it->second.stream));
        it = market_streams_.erase(it);
    }

    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        if (!entry.needs_pass)
            continue;
        entry.needs_pass = false;
        for (const auto series :
            series_.held(entry.instance_id, entry.account_id))
            reconcile(entry, series, refusals);
    }

    series_.drain_changes(changes_);
    for (const auto series : changes_) {
        const auto named = series_.key_of(series);
        if (!named) {
            forget(series);
            continue;
        }
        const auto stream = market_streams_.find(
            running_key(named->instance_id, named->account_id));
        if (stream != market_streams_.end())
            reconcile(stream->second, series, refusals);
    }

    const auto now = steady_now_ns();
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        if (entry.stream->take_reconnected()) {
            for (const auto series : entry.watched) {
                const auto named = *series_.key_of(series);
                read_backfill_(named.instance_id, named.account_id,
                    named.series_id);
            }
        }

        entry.stream->drain(buckets_);
        for (const auto& bucket : buckets_) {
            const auto applied = series_.append(bucket.series, bucket.level,
                model_state::series_point{.at_ns = bucket.at_ns,
                    .value = bucket.value, .open = bucket.open,
                    .high = bucket.high, .low = bucket.low},
                now);
            if (applied == model_state::append_result::stale)
                ++stale_buckets_;
            else if (applied == model_state::append_result::late)
                ++late_buckets_;
        }
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
