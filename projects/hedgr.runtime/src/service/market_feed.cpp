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
#include "hedgr.runtime/service/session_service.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <format>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace domain = hedgr::model::domain;
namespace model_state = hedgr::model::state;

using manager::connection_options_for;
using support::lg;
using support::running_key;
using support::steady_now_ns;

/** @brief Why a plot waits on a series the catalogue does not name. */
constexpr std::string_view unlisted_series_message =
    "This series is not in the instance's catalogue (the instance may have "
    "been re-provisioned). Close this window and open the series again from "
    "the series browser.";

}

market_feed::market_feed(const model::state::session_table& sessions,
    model::state::series_table& series, session_service& actors,
    catalogue_read read_catalogue, backfill_read read_backfill)
    : sessions_(sessions), series_(series), actors_(actors),
      read_catalogue_(std::move(read_catalogue)),
      read_backfill_(std::move(read_backfill)) {
}

void market_feed::attach(std::filesystem::path database_path) {
    database_path_ = std::move(database_path);
}

void market_feed::attach(std::string_view instance_id,
    std::string_view account_id, std::shared_ptr<worker::tick_feed> stream) {
    if (!is_open() || stream == nullptr)
        return;

    /*
     * The entry is the feed's own bookkeeping: which series it has told the
     * actor to watch, whether the full pass after a sign-in has run, and the
     * actor it says it to. The actor is held here so an idle pump costs no key
     * building; the session service holds its own reference, so a retired
     * actor lives until its thread has finished.
     */
    market_stream_entry entry;
    entry.instance_id = std::string{instance_id};
    entry.account_id = std::string{account_id};
    entry.stream = std::move(stream);
    if (const auto session = sessions_.find(instance_id, account_id))
        entry.session = *session;
    market_streams_.insert_or_assign(running_key(instance_id, account_id),
        std::move(entry));
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Market feed watching '" << instance_id << "' / '" << account_id
        << "'";
}

void market_feed::detach(std::string_view instance_id,
    std::string_view account_id) {
    market_streams_.erase(running_key(instance_id, account_id));
}

void market_feed::refresh_watches(std::string_view instance_id,
    std::string_view account_id) {
    const auto found = market_streams_.find(running_key(instance_id,
        account_id));
    if (found == market_streams_.end())
        return;
    found->second.needs_pass = true;
}

void market_feed::link_restored(std::string_view instance_id,
    std::string_view account_id) {
    const auto found = market_streams_.find(running_key(instance_id, account_id));
    if (found == market_streams_.end())
        return;

    /*
     * The ticks published while the connection was down are gone, so every
     * series this session watches is read again. A backfill merges with the
     * live buckets and never replaces a newer one, so a reseed that was not
     * needed costs a read and corrupts nothing.
     */
    for (const auto series : found->second.watched) {
        const auto named = series_.key_of(series);
        if (!named)
            continue;
        read_backfill_(named->instance_id, named->account_id,
            named->series_id);
    }
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
    if (info == nullptr || !info->listed) {
        /*
         * A catalogue that has been read and does not name the series is an
         * answer, not a pending read. A window restored from a layout that
         * outlived the series (an instance re-provisioned with new keys) would
         * otherwise wait for a backfill nothing will ever start, and its plot
         * would spin for good. The failure is recorded once, and a catalogue
         * that lists the series later puts it back on the change list.
         */
        const auto* catalogue =
            series_.catalogue(entry.instance_id, entry.account_id);
        if (catalogue->state == model::state::catalogue_state::ready
            && series_.failure(series).empty()) {
            series_.fail(series, std::string{unlisted_series_message},
                steady_now_ns());
        }
        return false;
    }

    /*
     * The tick subject is party scoped and the token is for the session's
     * party. A chart looking at another party through the picker keeps its
     * backfill and manual refresh, not a live stream.
     *
     * A window restored from a layout the instance no longer agrees with lands
     * here: its series is listed, but under a party this session does not act
     * as, because the instance was re-provisioned. Saying so is what turns a
     * plot that spins for good into one that explains itself, and a session
     * that later acts as that party clears it.
     */
    const auto session = sessions_.find(entry.instance_id, entry.account_id);
    if (!session)
        return false;
    if (info->party_id != sessions_.party_id(*session)) {
        std::string why = std::format(
            "This series belongs to party '{}' and the session acts as party "
            "'{}. The instance may have been re-provisioned: close this window "
            "and open the series again from the series browser.",
            info->party_id, sessions_.party_id(*session));
        if (series_.failure(series) != why) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Not watching series '" << series_.key_of(series)->series_id
                << "': it belongs to party '" << info->party_id
                << "' and the session is party '"
                << sessions_.party_id(*session) << "'";
            series_.fail(series, why, steady_now_ns());
        }
        return false;
    }
    return true;
}

void market_feed::reconcile(market_stream_entry& entry,
    std::vector<refusal>& refusals) {
    if (!entry.stream)
        return;

    std::vector<worker::watch_target> targets;
    std::vector<model::state::series_handle> watching;
    for (const auto series :
        series_.held(entry.instance_id, entry.account_id)) {
        if (!wanted(entry, series, refusals))
            continue;
        const auto* info = series_.info(series);
        if (info == nullptr)
            continue;
        const bool already = std::ranges::find(entry.watched, series)
            != entry.watched.end();
        if (!already) {
            const auto key = *series_.key_of(series);
            read_backfill_(key.instance_id, key.account_id, key.series_id);
        }
        auto canonical = session::market::canonical_series_uri(info->uri);
        if (!canonical) {
            const auto key = series_.key_of(series);
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Not watching series '"
                << (key ? key->series_id : std::string{"?"})
                << "' in party '" << info->party_id
                << "': its URI names no series: " << info->uri;
            continue;
        }
        if (!already) {
            const auto key = *series_.key_of(series);
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Watching series '" << key.series_id << "' as '"
                << *canonical << "' in party '" << info->party_id << "'";
        }
        watching.push_back(series);
        targets.push_back(
            {.series = series, .canonical_uri = std::move(*canonical)});
    }
    if (!entry.stream)
        return;
    entry.stream->set_watch(targets);
    entry.watched = std::move(watching);
}

void market_feed::forget(model::state::series_handle series) {
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        const auto watching = std::ranges::find(entry.watched, series);
        if (watching == entry.watched.end())
            continue;
        if (entry.stream)
            entry.stream->unwatch(series);
        entry.watched.erase(watching);
    }
}

std::vector<refusal> market_feed::pump() {
    std::vector<refusal> refusals;

    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        if (!entry.needs_pass)
            continue;
        entry.needs_pass = false;
        reconcile(entry, refusals);
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
            reconcile(stream->second, refusals);
    }

    const auto now = steady_now_ns();
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        if (!entry.stream)
            continue;
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

    actors_.reap();

    return refusals;
}

void market_feed::cancel() {
    market_streams_.clear();
    actors_.cancel();
}

}
