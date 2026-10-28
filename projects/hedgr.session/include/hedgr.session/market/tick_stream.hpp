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
#include "ores.nats/domain/wire_codec.hpp"
#include "ores.nats/service/subscription.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::session::market {

/**
 * @brief The one URI the codec writes for the series an oresmd URI names.
 *
 * A catalogue row names a series. A tick names a quote, which is the series
 * plus the coordinates of the observation, such as a swap's term. Both reduce
 * to the same series URI here, so a tick can be matched to the catalogue row
 * it belongs to by string equality. Nothing is returned for a URI the codec
 * refuses.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::optional<std::string>
canonical_series_uri(std::string_view oresmd_uri);

/** @brief One observation as the live tick stream carries it. */
struct tick final {
    /** @brief The series the observed quote belongs to, canonical. */
    std::string series_uri;

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
 * value that is not a finite number, a payload that is not a tick, or a quote
 * URI that names no series is refused rather than plotted as zero.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::optional<tick>
decode_tick(const std::vector<std::byte>& payload);

/**
 * @brief Decodes one tick payload with @p codec.
 *
 * A stream built for one deployment uses that deployment's format rather than
 * the process-wide default, so two sessions of different formats decode their
 * own ticks correctly.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::optional<tick>
decode_tick(const std::vector<std::byte>& payload,
    const ores::nats::wire_codec& codec);

/**
 * @brief The deployment's live prices for one party, on one subscription.
 *
 * One connection per session, resumed with the session's token, and one NATS
 * subscription covering every datum the party reads. A series is not a
 * subject a feed publishes on: its points each travel on a subject of their
 * own, named by the quote's canonical URI, so no wildcard covers exactly one
 * series. The party's whole tick subtree is read and each tick is handed up
 * with the series it belongs to, for the caller to keep or drop.
 *
 * One subscription also means one cnats delivery thread, so the handler runs
 * on one thread at a time.
 *
 * Not thread safe: the connection and the subscription are touched by the
 * thread that owns the stream. The handler runs on the NATS dispatch thread.
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

    /**
     * @brief Closes the connection, dropping the subscription.
     *
     * Returns only once a handler that was running has returned, so the
     * caller may free what the handler reaches as soon as this returns.
     */
    void disconnect();

    /**
     * @brief The token the next @c connect resumes with.
     *
     * A refreshed token does not need the connection reopened: the broker
     * does not see the token on a subscribe, only on the resume.
     */
    void set_token(std::string token);

    /**
     * @brief Subscribes to the party's ticks, replacing any subscription.
     *
     * @param handler called on the NATS dispatch thread for every tick.
     * @return whether the subscription was made.
     */
    bool subscribe(tick_handler handler);

    /** @brief Whether the party's ticks are subscribed. */
    [[nodiscard]] bool subscribed() const noexcept {
        return subscription_.has_value();
    }

    /** @brief Why the last connect or subscribe failed, or empty. */
    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }

private:
    /**
     * @brief The deployment's format, fixed for the stream's life.
     *
     * Before the client so the constructor can read the options before they
     * are moved into it.
     */
    ores::nats::wire_codec codec_;
    transport::session_client client_;
    std::string token_;
    std::string principal_;
    std::string tenant_id_;
    std::string party_id_;
    std::string last_error_;
    std::optional<ores::nats::service::subscription> subscription_;
};

}

#endif
