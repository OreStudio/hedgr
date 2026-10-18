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
#ifndef HEDGR_SESSION_MARKET_TICK_STREAM_HPP
#define HEDGR_SESSION_MARKET_TICK_STREAM_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "ores.nats/service/subscription.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::session::market {

/**
 * @brief The URI a feed publishes a series' ticks under.
 *
 * The tick subject names the datum the feed observed, which is a quote, while
 * the catalogue names the series. Subscribing to the series URI therefore
 * listens to a subject nothing publishes on. The one difference is the type
 * field.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
tick_uri_for_series(std::string_view series_uri);

/** @brief One observation as the live tick stream carries it. */
struct tick final {
    /** @brief The observation time, nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value. */
    double value = 0.0;
};

/** @brief What to do with each tick, on the NATS dispatch thread. */
using tick_handler = std::function<void(const tick&)>;

/**
 * @brief Decodes one tick payload, or nothing when it is unusable.
 *
 * Split out from the stream so the decode can be tested without a broker. A
 * value that is not a finite number, or a payload that is not a tick, is
 * refused rather than plotted as zero.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::optional<tick>
decode_tick(const std::vector<std::byte>& payload);

/**
 * @brief The deployment's live prices, subscribed per series.
 *
 * One connection per session, resumed with the session's token, and one NATS
 * subscription per watched series. The subject is the party-scoped tick
 * subject ORE publishes on, so the broker filters server side and the callback
 * knows which series it belongs to.
 *
 * Not thread safe: the connection and the subscription map are touched by the
 * thread that owns the stream. The handlers themselves run on the NATS
 * dispatch thread.
 */
class HEDGR_SESSION_EXPORT market_tick_stream final {
public:
    market_tick_stream(transport::session_options options, std::string token,
        std::string principal, std::string tenant_id, std::string party_id);
    ~market_tick_stream();

    market_tick_stream(const market_tick_stream&) = delete;
    market_tick_stream& operator=(const market_tick_stream&) = delete;

    /**
     * @brief Opens the connection and resumes the session.
     *
     * Blocking, so it belongs on the stream's own thread and never on the
     * frame loop. A reconnect is the same call after a disconnect.
     */
    bool connect();

    /** @brief Whether the connection is open and authenticated. */
    [[nodiscard]] bool connected() const;

    /** @brief Closes the connection, dropping every subscription. */
    void disconnect();

    /**
     * @brief Watches one series, replacing any handler already watching it.
     *
     * @param series_id the deployment's key for the series, used as the map key.
     * @param oresmd_uri the series' canonical URI, from the catalogue.
     * @param handler called on the NATS dispatch thread for every tick.
     * @return whether the subscription was made.
     */
    bool subscribe(std::string series_id, std::string_view oresmd_uri,
        tick_handler handler);

    /** @brief Stops watching one series, if it is watched. */
    void unsubscribe(std::string_view series_id);

    /** @brief The number of series currently watched. */
    [[nodiscard]] std::size_t watched() const noexcept {
        return subscriptions_.size();
    }

    /** @brief Why the last connect or subscribe failed, or empty. */
    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }

private:
    transport::session_client client_;
    std::string token_;
    std::string principal_;
    std::string tenant_id_;
    std::string party_id_;
    std::string last_error_;
    std::unordered_map<std::string, ores::nats::service::subscription>
        subscriptions_;
};

}

#endif
