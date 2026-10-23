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
#include "hedgr.session/transport/tick_span.hpp"

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include "hedgr.model/domain/market.hpp"
#include "hedgr.session/market/series_label.hpp"

namespace hedgr::session::transport {

namespace {

namespace domain = hedgr::model::domain;

/** @brief Refusals a first load shortens its span for, down to a quarter hour. */
constexpr unsigned first_load_retries = 3;

[[nodiscard]] session_errc errc_for(const std::string& code) noexcept {
    if (code == "range_too_large")
        return session_errc::range_too_large;
    if (code == "party_required")
        return session_errc::party_required;
    if (code == "range_inverted")
        return session_errc::range_inverted;
    if (code == "filter_too_large")
        return session_errc::filter_too_large;
    return session_errc::read_refused;
}

}

session_error refusal_from(const ores::utility::domain::result& result) {
    const auto code = errc_for(result.code);
    auto error = make_error(code, result.message);
    error.message = std::format("{} ({}){}{}", error.message,
        result.code.empty() ? std::string{"no outcome code"} : result.code,
        result.message.empty() ? "" : ": ", result.message);
    return error;
}

result<market_series_read> series_read_from_ticks(
    const ores::marketdata::messaging::get_series_ticks_response& reply) {
    if (reply.result.outcome != ores::utility::domain::outcome::ok)
        return std::unexpected(refusal_from(reply.result));

    const auto count = reply.values.size();
    market_series_read read;
    read.ticks = count;
    read.coordinates = reply.coordinates.size();
    if (count == 0)
        return read;

    if (reply.gaps_us.size() != count
        || reply.coordinate_indexes.size() != count || !reply.first_instant) {
        return fail(session_errc::protocol_error,
            std::format("the tick reply holds {} values but {} gaps and {} "
                        "coordinate indexes{}", count, reply.gaps_us.size(),
                reply.coordinate_indexes.size(),
                reply.first_instant ? "" : " and no first instant"));
    }
    if (reply.coordinates.empty())
        return fail(session_errc::protocol_error,
            "the tick reply names no coordinate");

    read.coordinate = reply.coordinates.front();
    const auto term = market::series_term(read.coordinate);
    auto at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        reply.first_instant->time_since_epoch()).count();
    read.points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        at_ns += reply.gaps_us[i] * 1000;
        const auto index = reply.coordinate_indexes[i];
        if (index < 0
            || static_cast<std::size_t>(index) >= reply.coordinates.size()) {
            return fail(session_errc::protocol_error,
                std::format("tick {} names coordinate {} of {}", i, index,
                    reply.coordinates.size()));
        }
        if (index == 0)
            read.points.push_back(market_point{.at_ns = at_ns,
                .value = reply.values[i], .term = term});
    }
    return read;
}

result<market_series_read> read_tick_span(const tick_fetch& fetch,
    tick_span span, std::int64_t now_ns, unsigned retries) {
    bool shortened = false;
    while (true) {
        auto read = fetch(span);
        if (!read) {
            if (read.error().code != session_errc::range_too_large
                || retries == 0)
                return std::unexpected(std::move(read.error()));
            --retries;
            const auto end = span.to_ns == 0 ? now_ns : span.to_ns;
            span.from_ns = end - (end - span.from_ns) / 2;
            shortened = true;
            continue;
        }
        read->from_ns = span.from_ns;
        read->to_ns = span.to_ns;
        read->shortened = shortened;
        read->truncated = shortened
            || read->ticks >= domain::market_tick_ceiling;
        return read;
    }
}

result<market_series_read> read_newest_ticks(const tick_fetch& fetch,
    std::int64_t now_ns, std::span<const std::int64_t> spans,
    std::size_t min_points) {
    std::optional<market_series_read> best;
    for (const auto length : spans) {
        auto read = read_tick_span(fetch,
            tick_span{.from_ns = now_ns - length}, now_ns, first_load_retries);
        if (!read) {
            if (best)
                return std::move(*best);
            return std::unexpected(std::move(read.error()));
        }
        best = std::move(*read);
        if (best->points.size() >= min_points || best->truncated)
            break;
    }
    return std::move(*best);
}

result<market_series_read> read_first_load(const tick_fetch& fetch,
    std::int64_t now_ns) {
    return read_newest_ticks(fetch, now_ns, domain::market_first_load_spans,
        domain::market_first_load_min_points);
}

}
