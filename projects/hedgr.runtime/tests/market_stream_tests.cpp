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
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.runtime/worker/tick_sink.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using hedgr::model::state::market_bucket;
using hedgr::runtime::worker::tick_sink;
using hedgr::session::market::canonical_series_uri;
using hedgr::session::market::tick;

constexpr auto fx_series =
    "oresmd://fx/GBP?type=series&instrument=fx_spot&quote=rate&ccy=USD";
constexpr auto swap_series =
    "oresmd://ir/GBP?type=series&instrument=ir_swap&quote=rate&fwd_start=0D&tenor=1D";
constexpr auto zero_series =
    "oresmd://ir/HUF?type=series&instrument=zero&quote=rate&curve_id=HUF1D&day_counter=A365";

/* A tick of @p series_uri's quote, as the stream hands it to the sink. */
tick tick_for(const char* series_uri, std::int64_t at_ns, double value) {
    const auto key = canonical_series_uri(series_uri);
    REQUIRE(key.has_value());
    return tick{.series_uri = *key, .at_ns = at_ns, .value = value};
}

std::vector<std::string> series_ids(std::vector<market_bucket>& buckets) {
    std::vector<std::string> ids;
    for (const auto& bucket : buckets)
        ids.push_back(bucket.series_id);
    buckets.clear();
    return ids;
}

/**
 * @brief A TCP port that accepts a connection and never speaks.
 *
 * cnats waits for the server's INFO line for its connect timeout, two
 * seconds by default, which is the longest a tick stream's thread can be
 * inside one connect attempt.
 */
class silent_listener final {
public:
    silent_listener() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) == 0);
        REQUIRE(::listen(fd_, 8) == 0);
        socklen_t length = sizeof(address);
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&address),
                    &length) == 0);
        port_ = ntohs(address.sin_port);
    }

    ~silent_listener() {
        ::close(fd_);
    }

    [[nodiscard]] std::string url() const {
        return "nats://127.0.0.1:" + std::to_string(port_);
    }

private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
};

}

TEST_CASE("the sink keeps the ticks of watched series and drops the rest",
    "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    std::vector<market_bucket> buckets;

    REQUIRE(sink.watch("fx-1", fx_series));
    REQUIRE(sink.watch("swap-1", swap_series));
    REQUIRE(sink.watched() == 2);

    /*
     * One tick opens a bucket, a tick in the next second closes it. The zero
     * curve is not watched, so its ticks must leave nothing behind.
     */
    for (const auto* series : {fx_series, swap_series, zero_series}) {
        sink.on_tick(tick_for(series, 1 * second, 1.0));
        sink.on_tick(tick_for(series, 2 * second, 2.0));
    }
    REQUIRE(sink.ticks() == 4);
    REQUIRE(sink.drain(buckets) == 2);
    const auto closed = series_ids(buckets);
    CHECK(closed == std::vector<std::string>{"fx-1", "swap-1"});

    sink.unwatch("swap-1");
    REQUIRE(sink.watched() == 1);
    for (const auto* series : {fx_series, swap_series}) {
        sink.on_tick(tick_for(series, 3 * second, 3.0));
        sink.on_tick(tick_for(series, 4 * second, 4.0));
    }
    REQUIRE(sink.drain(buckets) == 2);
    CHECK(series_ids(buckets) == std::vector<std::string>{"fx-1", "fx-1"});

    /* Watching again continues the series. */
    REQUIRE(sink.watch("swap-1", swap_series));
    sink.on_tick(tick_for(swap_series, 5 * second, 5.0));
    sink.on_tick(tick_for(swap_series, 6 * second, 6.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series_id == "swap-1");
    CHECK(buckets.front().point.value == 5.0);
}

TEST_CASE("a URI that names no series is not watched", "[market_stream]") {
    tick_sink sink;
    CHECK_FALSE(sink.watch("odd-1", "not-a-uri"));
    CHECK(sink.watched() == 0);
}

TEST_CASE("asking a stream to stop does not wait for its connect",
    "[market_stream]") {
    using namespace std::chrono;
    using hedgr::runtime::worker::market_stream;
    using hedgr::runtime::worker::market_stream_options;

    silent_listener listener;
    market_stream_options options;
    options.options.nats_url = listener.url();
    options.options.subject_prefix = "hedgr";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";

    market_stream stream(std::move(options));
    stream.start();

    /* The thread connects at once, so by now it is inside the connect. */
    std::this_thread::sleep_for(milliseconds{300});
    REQUIRE_FALSE(stream.finished());

    const auto asked = steady_clock::now();
    stream.request_stop();
    const auto returned = steady_clock::now();
    CHECK(returned - asked < milliseconds{200});

    const auto deadline = asked + seconds{10};
    while (!stream.finished() && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds{20});
    CHECK(stream.finished());

    /*
     * The connect ran to its timeout. A stop that waited for it would have
     * taken at least this long to return.
     */
    const auto finished_at = steady_clock::now();
    CHECK(finished_at - asked >= seconds{1});
}

TEST_CASE("watch changes and ticks interleave safely", "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    REQUIRE(sink.watch("fx-1", fx_series));

    std::thread watcher([&sink]() {
        for (int i = 0; i < 2000; ++i) {
            REQUIRE(sink.watch("swap-1", swap_series));
            sink.unwatch("swap-1");
        }
    });
    std::int64_t at = second;
    for (int i = 0; i < 2000; ++i, at += second) {
        sink.on_tick(tick_for(fx_series, at, 1.0));
        sink.on_tick(tick_for(swap_series, at, 2.0));
    }
    watcher.join();

    std::vector<market_bucket> buckets;
    REQUIRE(sink.drain(buckets) > 0);
    std::size_t fx = 0;
    for (const auto& bucket : buckets)
        fx += bucket.series_id == "fx-1";
    CHECK(fx >= 1999);
    CHECK(sink.ticks() >= 2000);
}
