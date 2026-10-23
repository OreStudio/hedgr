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
#include "hedgr.model/domain/market.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace transport = hedgr::session::transport;
namespace domain = hedgr::model::domain;
using ores::marketdata::messaging::get_series_ticks_response;
using transport::session_errc;

namespace {

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t hour = 3600 * second;
constexpr std::int64_t now = 1'800'000'000 * second;
constexpr auto eurusd = "oresmd://fx/EURUSD?ccy=USD";
constexpr auto gbpusd = "oresmd://fx/GBPUSD?ccy=USD";

std::chrono::system_clock::time_point at_us(std::int64_t micros) {
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(micros))};
}

get_series_ticks_response refusal(std::string code, std::string message = "m") {
    get_series_ticks_response reply;
    reply.result.outcome = ores::utility::domain::outcome::failed;
    reply.result.code = std::move(code);
    reply.result.message = std::move(message);
    return reply;
}

/** @brief A fetch answering with @p count ticks, recording every span asked. */
struct fake_fetch final {
    std::vector<transport::tick_span> asked;
    std::size_t count = 0;
    std::size_t ticks = 0;
    int too_large_until = 0;
    std::vector<std::size_t> counts_by_call;

    transport::result<transport::market_series_read> operator()(
        const transport::tick_span& span) {
        asked.push_back(span);
        if (static_cast<int>(asked.size()) <= too_large_until)
            return transport::fail(session_errc::range_too_large, "limit");
        const auto n = counts_by_call.empty()
            ? count : counts_by_call[std::min(asked.size() - 1,
                  counts_by_call.size() - 1)];
        transport::market_series_read read;
        read.points.resize(n);
        read.ticks = ticks == 0 ? n : ticks;
        return read;
    }
};

}

TEST_CASE("the times are the first instant plus the running sum of the gaps",
    "[ticks]") {
    get_series_ticks_response reply;
    reply.coordinates = {eurusd};
    reply.first_instant = at_us(1'700'000'000'000'000);
    reply.gaps_us = {0, 1'500'000, 250};
    reply.coordinate_indexes = {0, 0, 0};
    reply.values = {1.10, 1.11, 1.12};

    const auto read = transport::series_read_from_ticks(reply);

    REQUIRE(read);
    REQUIRE(read->points.size() == 3);
    CHECK(read->points[0].at_ns == 1'700'000'000 * second);
    CHECK(read->points[1].at_ns == 1'700'000'001 * second + 500'000'000);
    CHECK(read->points[2].at_ns == 1'700'000'001 * second + 500'250'000);
    CHECK(read->points[2].value == 1.12);
    CHECK(read->coordinate == eurusd);
    CHECK(read->ticks == 3);
}

TEST_CASE("a reply of several coordinates plots the first and counts them all",
    "[ticks]") {
    get_series_ticks_response reply;
    reply.coordinates = {eurusd, gbpusd};
    reply.first_instant = at_us(1'000'000);
    reply.gaps_us = {0, 1'000'000, 1'000'000, 1'000'000};
    reply.coordinate_indexes = {0, 1, 0, 1};
    reply.values = {1.0, 2.0, 3.0, 4.0};

    const auto read = transport::series_read_from_ticks(reply);

    REQUIRE(read);
    REQUIRE(read->points.size() == 2);
    CHECK(read->points[0].value == 1.0);
    CHECK(read->points[1].value == 3.0);
    CHECK(read->points[1].at_ns == 3 * second);
    CHECK(read->coordinate == eurusd);
    CHECK(read->coordinates == 2);
    CHECK(read->ticks == 4);
}

TEST_CASE("an empty reply is a read with no points, not a failure", "[ticks]") {
    const auto read = transport::series_read_from_ticks({});

    REQUIRE(read);
    CHECK(read->points.empty());
    CHECK(read->ticks == 0);
    CHECK(read->coordinate.empty());
}

TEST_CASE("each refusal is its own failure and names its outcome code",
    "[ticks]") {
    const std::vector<std::pair<std::string, session_errc>> cases{
        {"range_too_large", session_errc::range_too_large},
        {"party_required", session_errc::party_required},
        {"range_inverted", session_errc::range_inverted},
        {"filter_too_large", session_errc::filter_too_large},
        {"internal_error", session_errc::read_refused}};
    for (const auto& [code, errc] : cases) {
        INFO(code);
        const auto read = transport::series_read_from_ticks(
            refusal(code, "at most 25000"));
        REQUIRE_FALSE(read);
        CHECK(read.error().code == errc);
        CHECK(read.error().message.find("(" + code + ")")
            != std::string::npos);
        CHECK(read.error().message.find("at most 25000") != std::string::npos);
    }
}

TEST_CASE("a reply whose arrays disagree is a protocol error", "[ticks]") {
    get_series_ticks_response reply;
    reply.coordinates = {eurusd};
    reply.first_instant = at_us(0);
    reply.gaps_us = {0};
    reply.coordinate_indexes = {0, 0};
    reply.values = {1.0, 2.0};
    const auto short_gaps = transport::series_read_from_ticks(reply);
    REQUIRE_FALSE(short_gaps);
    CHECK(short_gaps.error().code == session_errc::protocol_error);

    reply.gaps_us = {0, 1};
    reply.coordinate_indexes = {0, 3};
    const auto bad_index = transport::series_read_from_ticks(reply);
    REQUIRE_FALSE(bad_index);
    CHECK(bad_index.error().code == session_errc::protocol_error);
}

TEST_CASE("a refusal as too large retries once on the newer half of the span",
    "[ticks]") {
    fake_fetch fetch{.count = 50, .too_large_until = 1};

    const auto read = transport::read_tick_span(std::ref(fetch),
        {.from_ns = now - 4 * hour, .to_ns = now}, now, 1);

    REQUIRE(read);
    REQUIRE(fetch.asked.size() == 2);
    CHECK(fetch.asked[0].from_ns == now - 4 * hour);
    CHECK(fetch.asked[1].from_ns == now - 2 * hour);
    CHECK(fetch.asked[1].to_ns == now);
    CHECK(read->shortened);
    CHECK(read->truncated);
    CHECK(read->from_ns == now - 2 * hour);
    CHECK(read->points.size() == 50);
}

TEST_CASE("a span with no end halves from the clock", "[ticks]") {
    fake_fetch fetch{.count = 5, .too_large_until = 1};

    const auto read = transport::read_tick_span(std::ref(fetch),
        {.from_ns = now - 2 * hour}, now, 1);

    REQUIRE(read);
    CHECK(fetch.asked[1].from_ns == now - hour);
    CHECK(fetch.asked[1].to_ns == 0);
}

TEST_CASE("the retries are bounded and a second refusal is the failure",
    "[ticks]") {
    fake_fetch fetch{.count = 5, .too_large_until = 5};

    const auto read = transport::read_tick_span(std::ref(fetch),
        {.from_ns = now - 4 * hour, .to_ns = now}, now, 1);

    REQUIRE_FALSE(read);
    CHECK(read.error().code == session_errc::range_too_large);
    CHECK(fetch.asked.size() == 2);
}

TEST_CASE("a refusal other than too large is not retried", "[ticks]") {
    std::size_t calls = 0;
    const transport::tick_fetch fetch = [&](const transport::tick_span&) {
        ++calls;
        return transport::result<transport::market_series_read>(
            transport::fail(session_errc::party_required, "no party"));
    };

    const auto read = transport::read_tick_span(fetch,
        {.from_ns = now - hour, .to_ns = now}, now, 3);

    REQUIRE_FALSE(read);
    CHECK(read.error().code == session_errc::party_required);
    CHECK(calls == 1);
}

TEST_CASE("a reply at the ceiling is marked truncated, one under is not",
    "[ticks]") {
    fake_fetch under{.count = 10, .ticks = domain::market_tick_ceiling - 1};
    fake_fetch at{.count = 10, .ticks = domain::market_tick_ceiling};

    const auto a = transport::read_tick_span(std::ref(under),
        {.from_ns = now - hour, .to_ns = now}, now, 1);
    const auto b = transport::read_tick_span(std::ref(at),
        {.from_ns = now - hour, .to_ns = now}, now, 1);

    REQUIRE(a);
    REQUIRE(b);
    CHECK_FALSE(a->truncated);
    CHECK(b->truncated);
    CHECK_FALSE(b->shortened);
}

TEST_CASE("the first load asks the two hours the fine level keeps, to now",
    "[ticks][first-load]") {
    fake_fetch fetch{.count = 500};

    const auto read = transport::read_first_load(std::ref(fetch), now);

    REQUIRE(read);
    REQUIRE(fetch.asked.size() == 1);
    CHECK(fetch.asked[0].from_ns == now - 2 * hour);
    CHECK(fetch.asked[0].to_ns == 0);
    CHECK(read->points.size() == 500);
}

TEST_CASE("a slow series widens the first load until it holds 200 points",
    "[ticks][first-load]") {
    fake_fetch fetch{.counts_by_call = {3, 60, 240}};

    const auto read = transport::read_first_load(std::ref(fetch), now);

    REQUIRE(read);
    REQUIRE(fetch.asked.size() == 3);
    CHECK(fetch.asked[1].from_ns == now - 48 * hour);
    CHECK(fetch.asked[2].from_ns == now - 720 * hour);
    CHECK(read->points.size() == 240);
}

TEST_CASE("a series with fewer than 200 points in thirty days gets them all",
    "[ticks][first-load]") {
    fake_fetch fetch{.counts_by_call = {0, 4, 9}};

    const auto read = transport::read_first_load(std::ref(fetch), now);

    REQUIRE(read);
    CHECK(fetch.asked.size() == 3);
    CHECK(read->points.size() == 9);
}

TEST_CASE("a dense series is not widened and keeps its shortened read",
    "[ticks][first-load]") {
    fake_fetch fetch{.count = 100, .too_large_until = 1};

    const auto read = transport::read_first_load(std::ref(fetch), now);

    REQUIRE(read);
    CHECK(fetch.asked.size() == 2);
    CHECK(read->truncated);
    CHECK(read->from_ns == now - hour);
}

TEST_CASE("a wider read that fails keeps the narrower one that arrived",
    "[ticks][first-load]") {
    std::size_t calls = 0;
    const transport::tick_fetch fetch = [&](const transport::tick_span&)
        -> transport::result<transport::market_series_read> {
        if (++calls == 2)
            return transport::fail(session_errc::timeout, "slow");
        transport::market_series_read read;
        read.points.resize(7);
        read.ticks = 7;
        return read;
    };

    const auto read = transport::read_first_load(fetch, now);

    REQUIRE(read);
    CHECK(read->points.size() == 7);
}

TEST_CASE("a first load that fails at once reports the failure",
    "[ticks][first-load]") {
    const transport::tick_fetch fetch = [](const transport::tick_span&) {
        return transport::result<transport::market_series_read>(
            transport::fail(session_errc::party_required, "no party"));
    };

    const auto read = transport::read_first_load(fetch, now);

    REQUIRE_FALSE(read);
    CHECK(read.error().code == session_errc::party_required);
}

TEST_CASE("a tail read starts at a minute and stops at the points it wants",
    "[ticks][tail]") {
    fake_fetch fetch{.counts_by_call = {4, 30}};

    const auto read = transport::read_newest_ticks(std::ref(fetch), now,
        domain::market_tail_spans, 20);

    REQUIRE(read);
    REQUIRE(fetch.asked.size() == 2);
    CHECK(fetch.asked[0].from_ns == now - 60 * second);
    CHECK(fetch.asked[1].from_ns == now - hour);
    CHECK(read->points.size() == 30);
}
