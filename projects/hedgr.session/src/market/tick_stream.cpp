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
#include "hedgr.session/market/tick_stream.hpp"
#include "ores.utility/rfl/reflectors.hpp" // IWYU pragma: keep.
#include "ores.logging/make_logger.hpp"
#include "ores.marketdata.api/datum/market_datum.hpp"
#include "ores.marketdata.api/domain/tick_subjects.hpp"
#include "ores.marketdata.api/messaging/operations_protocol.hpp"
#include "ores.marketdata.core/datum/oresmd_uri_codec.hpp"
#include "ores.nats/domain/wire_codec.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance =
        make_logger("hedgr.session.market.tick_stream");
    return instance;
}

}

namespace hedgr::session::market {

std::optional<std::string> canonical_series_uri(std::string_view oresmd_uri) {
    using ores::marketdata::datum::oresmd_uri_codec;
    const auto datum = oresmd_uri_codec::read(oresmd_uri);
    if (!datum)
        return std::nullopt;
    const auto written = oresmd_uri_codec::write(datum->is_series()
            ? *datum
            : ores::marketdata::datum::series_of(*datum));
    if (!written)
        return std::nullopt;
    return *written;
}

std::optional<tick> decode_tick(const std::vector<std::byte>& payload,
    const ores::nats::wire_codec& codec) {
    const auto decoded =
        codec.decode<ores::marketdata::messaging::market_tick>(payload);
    if (!decoded)
        return std::nullopt;

    char* end = nullptr;
    const auto value = std::strtod(decoded->value.c_str(), &end);
    if (end == decoded->value.c_str() || !std::isfinite(value))
        return std::nullopt;

    const auto at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        decoded->observation_time.time_since_epoch())
                           .count();
    if (at_ns <= 0)
        return std::nullopt;

    auto series_uri = canonical_series_uri(decoded->oresmd_uri);
    if (!series_uri)
        return std::nullopt;
    return tick{.series_uri = std::move(*series_uri), .at_ns = at_ns,
        .value = value};
}

std::optional<tick> decode_tick(const std::vector<std::byte>& payload) {
    return decode_tick(payload, ores::nats::default_wire_codec());
}

market_tick_stream::market_tick_stream(transport::session_options options,
    std::string token, std::string principal, std::string tenant_id,
    std::string party_id)
    : codec_(options.wire_format == "json"
              ? ores::nats::wire_format::json
              : ores::nats::wire_format::msgpack)
    , client_(std::move(options))
    , token_(std::move(token))
    , principal_(std::move(principal))
    , tenant_id_(std::move(tenant_id))
    , party_id_(std::move(party_id)) {
}

market_tick_stream::~market_tick_stream() {
    disconnect();
}

bool market_tick_stream::connect() {
    if (connected())
        return true;

    last_error_.clear();
    if (auto opened = client_.connect(); !opened) {
        last_error_ = opened.error().message;
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Tick stream could not connect: " << last_error_;
        return false;
    }
    if (auto resumed = client_.resume(token_, principal_); !resumed) {
        last_error_ = resumed.error().message;
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Tick stream could not resume the session: " << last_error_;
        client_.disconnect();
        return false;
    }
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Tick stream connected for party '" << party_id_ << "'";
    return true;
}

bool market_tick_stream::connected() const {
    return client_.is_connected() && client_.is_authenticated();
}

void market_tick_stream::disconnect() {
    /*
     * The subscription handle's destructor drains and waits for a handler
     * that is running, so the connection closes under no handler and the
     * caller's state is free to go once this returns.
     */
    subscription_.reset();
    client_.disconnect();
}

void market_tick_stream::set_token(std::string token) {
    token_ = std::move(token);
}

bool market_tick_stream::subscribe(tick_handler handler) {
    if (!connected()) {
        last_error_ = "the tick stream is not connected";
        return false;
    }

    try {
        const auto subject =
            ores::marketdata::domain::market_tick_subject_all(tenant_id_,
                party_id_);
        auto warned = std::make_shared<std::atomic<bool>>(false);
        auto subscription = client_.subscribe(subject,
            [handler = std::move(handler), warned, subject,
                codec = codec_](ores::nats::message msg) {
                if (auto tick = decode_tick(msg.data, codec)) {
                    handler(*tick);
                } else if (!warned->exchange(true)) {
                    BOOST_LOG_SEV(lg(), ores::logging::warn)
                        << "Could not decode a market tick on '" << subject
                        << "' (" << msg.data.size() << " bytes)";
                }
            });
        subscription_.reset();
        subscription_.emplace(std::move(subscription));
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Tick subscription failed: " << last_error_;
        return false;
    }
}

}
