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
#include "allocation_counter.hpp"
#include "fake_link.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include "hedgr.runtime/worker/tick_sink.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using hedgr::model::state::series_bucket;
using hedgr::model::state::series_handle;
using hedgr::runtime::worker::tick_sink;
using hedgr::runtime::worker::watch_target;
using hedgr::session::market::canonical_series_uri;
using hedgr::session::market::tick;

constexpr series_handle fx_handle{1, 1};
constexpr series_handle swap_handle{2, 1};

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

std::vector<series_handle> series_of_buckets(
    std::vector<series_bucket>& buckets) {
    std::vector<series_handle> handles;
    for (const auto& bucket : buckets)
        handles.push_back(bucket.series);
    buckets.clear();
    return handles;
}

/** @brief The canonical URI the feed hands the sink for @p series_uri. */
std::string canonical(const char* series_uri) {
    const auto key = canonical_series_uri(series_uri);
    REQUIRE(key.has_value());
    return *key;
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
    std::vector<series_bucket> buckets;

    sink.watch(fx_handle, canonical(fx_series));
    sink.watch(swap_handle, canonical(swap_series));
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
    const auto closed = series_of_buckets(buckets);
    CHECK(std::ranges::count(closed, fx_handle) == 1);
    CHECK(std::ranges::count(closed, swap_handle) == 1);

    sink.unwatch(swap_handle);
    REQUIRE(sink.watched() == 1);
    for (const auto* series : {fx_series, swap_series}) {
        sink.on_tick(tick_for(series, 3 * second, 3.0));
        sink.on_tick(tick_for(series, 4 * second, 4.0));
    }
    REQUIRE(sink.drain(buckets) == 2);
    CHECK(series_of_buckets(buckets)
        == std::vector<series_handle>{fx_handle, fx_handle});

    /* Watching again continues the series. */
    sink.watch(swap_handle, canonical(swap_series));
    sink.on_tick(tick_for(swap_series, 5 * second, 5.0));
    sink.on_tick(tick_for(swap_series, 6 * second, 6.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == swap_handle);
    CHECK(buckets.front().value == 5.0);
}

TEST_CASE("set_watch makes the given set the whole watch list",
    "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    std::vector<series_bucket> buckets;

    sink.watch(fx_handle, canonical(fx_series));
    sink.watch(swap_handle, canonical(swap_series));
    REQUIRE(sink.watched() == 2);

    const std::vector<watch_target> only_fx{
        {.series = fx_handle, .canonical_uri = canonical(fx_series)}};
    sink.set_watch(only_fx);
    REQUIRE(sink.watched() == 1);

    for (const auto* series : {fx_series, swap_series}) {
        sink.on_tick(tick_for(series, 1 * second, 1.0));
        sink.on_tick(tick_for(series, 2 * second, 2.0));
    }
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == fx_handle);

    /* Putting the second series back watches it from now on. */
    const std::vector<watch_target> both{
        {.series = fx_handle, .canonical_uri = canonical(fx_series)},
        {.series = swap_handle, .canonical_uri = canonical(swap_series)}};
    sink.set_watch(both);
    REQUIRE(sink.watched() == 2);
    sink.on_tick(tick_for(swap_series, 3 * second, 3.0));
    sink.on_tick(tick_for(swap_series, 4 * second, 4.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == swap_handle);
}

TEST_CASE("a bucket of a series that is not watched is not published",
    "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    std::vector<series_bucket> buckets;

    /* The handle was never watched, and unwatching it is harmless. */
    sink.unwatch(fx_handle);
    sink.on_tick(tick_for(fx_series, 1 * second, 1.0));
    sink.on_tick(tick_for(fx_series, 2 * second, 2.0));
    CHECK(sink.drain(buckets) == 0);

    /* Unwatching one series leaves another's aggregator alone. */
    sink.watch(fx_handle, canonical(fx_series));
    sink.watch(swap_handle, canonical(swap_series));
    sink.unwatch(swap_handle);
    CHECK(sink.watched() == 1);
    sink.on_tick(tick_for(fx_series, 3 * second, 1.0));
    sink.on_tick(tick_for(fx_series, 4 * second, 2.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == fx_handle);

    /* A handle of the same slot and another generation is another series. */
    const series_handle reused{fx_handle.index, fx_handle.generation + 1};
    sink.unwatch(reused);
    CHECK(sink.watched() == 1);

    /*
     * The new occupant's watch can reach the sink before the old occupant's
     * unwatch does. They are two series, so neither replaces the other.
     */
    sink.watch(reused, canonical(zero_series));
    CHECK(sink.watched() == 2);
    sink.unwatch(fx_handle);
    CHECK(sink.watched() == 1);
    sink.on_tick(tick_for(zero_series, 5 * second, 1.0));
    sink.on_tick(tick_for(zero_series, 6 * second, 2.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == reused);
}

TEST_CASE("watching one series under a new URI replaces its aggregator",
    "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    std::vector<series_bucket> buckets;

    sink.watch(fx_handle, canonical(fx_series));
    sink.on_tick(tick_for(fx_series, 1 * second, 1.0));

    /* The same URI again changes nothing, so the open bucket continues. */
    sink.watch(fx_handle, canonical(fx_series));
    sink.on_tick(tick_for(fx_series, 2 * second, 2.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().value == 1.0);
    buckets.clear();

    /* A new URI replaces it: the old URI is no longer routed and its open
     * bucket is gone, not published late. */
    sink.watch(fx_handle, canonical(swap_series));
    REQUIRE(sink.watched() == 1);
    sink.on_tick(tick_for(fx_series, 3 * second, 3.0));
    sink.on_tick(tick_for(fx_series, 4 * second, 4.0));
    CHECK(sink.drain(buckets) == 0);
    sink.on_tick(tick_for(swap_series, 5 * second, 5.0));
    sink.on_tick(tick_for(swap_series, 6 * second, 6.0));
    REQUIRE(sink.drain(buckets) == 1);
    CHECK(buckets.front().series == fx_handle);
    CHECK(buckets.front().value == 5.0);
}

TEST_CASE("a tick that closes a bucket allocates nothing", "[market_stream][cost]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    sink.watch(fx_handle, canonical(fx_series));
    std::vector<series_bucket> buckets;
    buckets.reserve(4096);

    /* The first tick of a series logs, so the loop warms up before it counts. */
    std::vector<tick> ticks;
    for (int s = 1; s <= 3000; ++s)
        ticks.push_back(tick_for(fx_series, s * second, s));
    sink.on_tick(ticks.front());
    sink.on_tick(ticks[1]);
    sink.drain(buckets);

    const auto before = hedgr::testing::allocations();
    for (std::size_t i = 2; i < ticks.size(); ++i)
        sink.on_tick(ticks[i]);
    CHECK(hedgr::testing::allocations() - before == 0);
    CHECK(sink.drain(buckets) >= 2990);
    CHECK(buckets.back().series == fx_handle);
}

TEST_CASE("asking a stream to stop does not wait for its connect",
    "[market_stream]") {
    using namespace std::chrono;
    using hedgr::runtime::worker::session_actor;
    using hedgr::runtime::worker::session_actor_options;

    silent_listener listener;
    session_actor_options options;
    options.options.nats_url = listener.url();
    options.options.subject_prefix = "hedgr";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";

    session_actor stream(std::move(options));
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

TEST_CASE("a read past the queue's capacity is answered and not queued",
    "[market_stream][reads]") {
    using hedgr::model::command::event_queue;
    using hedgr::model::event::event_kind;
    using hedgr::runtime::worker::image_request;
    using hedgr::runtime::worker::session_actor;
    using hedgr::runtime::worker::session_actor_options;

    event_queue events;
    session_actor_options options;
    options.options.wire_format = "json";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";
    options.key = hedgr::model::domain::session_key{"i-1", "a-1"};
    options.events = &events;
    options.max_queued_reads = 1;

    /*
     * The actor is never started, so nothing drains the queue: the second read
     * finds it full and is refused with the answer the frame loop needs.
     */
    session_actor actor(std::move(options));
    REQUIRE(actor.submit_read(image_request{.image_id = "img-1"}));
    CHECK(events.drain().empty());

    REQUIRE(actor.submit_read(image_request{.image_id = "img-2"}));
    const auto refused = events.drain();
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].kind == event_kind::image_loaded);
    CHECK(refused[0].image_id == "img-2");
    CHECK_FALSE(refused[0].message.empty());
}

TEST_CASE("a read queued on a stream that stops is answered once",
    "[market_stream][reads]") {
    using namespace std::chrono;
    using hedgr::model::command::event_queue;
    using hedgr::model::event::event_kind;
    using hedgr::runtime::worker::image_request;
    using hedgr::runtime::worker::session_actor;
    using hedgr::runtime::worker::session_actor_options;

    event_queue events;
    session_actor_options options;
    options.options.nats_url = "nats://127.0.0.1:1";
    options.options.wire_format = "json";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";
    options.key = hedgr::model::domain::session_key{"i-1", "a-1"};
    options.events = &events;

    session_actor stream(std::move(options));
    stream.start();

    /*
     * The read is answered whether it reaches a connection or not: the caller
     * marked it in flight, and only an answer clears that mark.
     */
    REQUIRE(stream.submit_read(image_request{.image_id = "img-1"}));
    stream.request_stop();

    const auto deadline = steady_clock::now() + seconds{10};
    while (!stream.finished() && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds{10});
    REQUIRE(stream.finished());

    const auto answers = events.drain();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].kind == event_kind::image_loaded);
    CHECK(answers[0].image_id == "img-1");
    CHECK(answers[0].instance_id == "i-1");
    CHECK(answers[0].account_id == "a-1");
    CHECK_FALSE(answers[0].message.empty());
}

TEST_CASE("a read is answered on the connection the actor was given",
    "[market_stream][reads]") {
    using namespace std::chrono;
    using hedgr::model::command::event_queue;
    using hedgr::model::event::event_kind;
    using hedgr::runtime::worker::image_request;
    using hedgr::runtime::worker::session_actor;
    using hedgr::runtime::worker::session_actor_options;
    using fake_link = hedgr::testing::fake_session_link;

    auto link = std::make_shared<fake_link>();
    link->image.mime_type = "image/png";
    link->image.data = {std::uint8_t{7}, std::uint8_t{8}};

    event_queue events;
    session_actor_options options;
    options.options.wire_format = "json";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";
    options.key = hedgr::model::domain::session_key{"i-1", "a-1"};
    options.events = &events;
    options.link = link;

    session_actor actor(std::move(options));

    /* Queued before the thread starts, so the first loop answers it. */
    REQUIRE(actor.submit_read(image_request{.image_id = "img-1"}));
    actor.start();

    std::vector<hedgr::model::event::event> answers;
    const auto deadline = steady_clock::now() + seconds{10};
    while (answers.empty() && steady_clock::now() < deadline) {
        answers = events.drain();
        std::this_thread::sleep_for(milliseconds{5});
    }

    actor.request_stop();
    const auto stopped = steady_clock::now() + seconds{10};
    while (!actor.finished() && steady_clock::now() < stopped)
        std::this_thread::sleep_for(milliseconds{5});
    REQUIRE(actor.finished());

    REQUIRE(answers.size() == 1);
    CHECK(answers[0].kind == event_kind::image_loaded);
    CHECK(answers[0].image_id == "img-1");
    CHECK(answers[0].bytes == std::vector<std::uint8_t>{7, 8});
    CHECK(answers[0].message.empty());

    /* The scripted connection is the one it used, and it opened it once. */
    CHECK(link->connects == 1);
    CHECK(link->resumes == 1);
}

namespace {

/** @brief Options for an actor driven by @p link, writing to @p events. */
hedgr::runtime::worker::session_actor_options fake_options(
    std::shared_ptr<hedgr::testing::fake_session_link> link,
    hedgr::model::command::event_queue& events) {
    hedgr::runtime::worker::session_actor_options options;
    options.options.wire_format = "json";
    options.token = "token";
    options.principal = "principal";
    options.tenant_id = "tenant";
    options.party_id = "party";
    options.key = hedgr::model::domain::session_key{"i-1", "a-1"};
    options.events = &events;
    options.link = std::move(link);
    return options;
}

/** @brief Everything the actor pushed, waiting up to @p timeout. */
std::vector<hedgr::model::event::event> next_events(
    hedgr::model::command::event_queue& events,
    std::chrono::milliseconds timeout = std::chrono::seconds{10}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto got = events.drain();
        if (!got.empty())
            return got;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return {};
}

/** @brief Waits for @p done, or gives up after ten seconds. */
template <class Predicate>
bool wait_until(Predicate done) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return done();
}

}

TEST_CASE("an outage is reported once and its recovery once",
    "[market_stream][liveness]") {
    using namespace std::chrono;
    using hedgr::model::event::event_kind;
    using hedgr::runtime::worker::session_actor;
    using fake_link = hedgr::testing::fake_session_link;

    auto link = std::make_shared<fake_link>();
    link->ping_answers = false;
    hedgr::model::command::event_queue events;
    auto options = fake_options(link, events);
    options.ping_interval = milliseconds{1};
    options.failures_before_reconnect = 0;

    session_actor actor(std::move(options));
    actor.start();

    const auto lost = next_events(events);
    REQUIRE(lost.size() == 1);
    CHECK(lost[0].kind == event_kind::link_lost);
    CHECK(lost[0].instance_id == "i-1");
    CHECK(lost[0].account_id == "a-1");

    /*
     * The link keeps failing: the outage is reported once and not once per
     * liveness request, however many of them fail while it lasts.
     */
    std::this_thread::sleep_for(milliseconds{50});
    CHECK(events.drain().empty());

    link->ping_answers = true;
    const auto restored = next_events(events);
    REQUIRE(restored.size() == 1);
    CHECK(restored[0].kind == event_kind::link_restored);

    std::this_thread::sleep_for(milliseconds{50});
    CHECK(events.drain().empty());

    actor.request_stop();
    CHECK(wait_until([&actor] { return actor.finished(); }));
}

TEST_CASE("a run of failed liveness requests rebuilds the connection",
    "[market_stream][liveness]") {
    using namespace std::chrono;
    using hedgr::runtime::worker::session_actor;
    using fake_link = hedgr::testing::fake_session_link;

    auto link = std::make_shared<fake_link>();
    link->ping_answers = false;
    hedgr::model::command::event_queue events;
    auto options = fake_options(link, events);
    options.ping_interval = milliseconds{1};
    options.failures_before_reconnect = 3;

    session_actor actor(std::move(options));
    actor.start();

    CHECK(wait_until([&link] { return link->disconnects >= 1; }));
    CHECK(wait_until([&link] { return link->connects >= 2; }));

    actor.request_stop();
    CHECK(wait_until([&actor] { return actor.finished(); }));
}

TEST_CASE("a stop does not wait for a liveness request in flight",
    "[market_stream][liveness]") {
    using namespace std::chrono;
    using hedgr::runtime::worker::session_actor;
    using fake_link = hedgr::testing::fake_session_link;

    auto link = std::make_shared<fake_link>();
    link->ping_delay = milliseconds{300};
    hedgr::model::command::event_queue events;
    auto options = fake_options(link, events);
    options.ping_interval = milliseconds{1};
    options.failures_before_reconnect = 0;

    session_actor actor(std::move(options));
    actor.start();
    REQUIRE(wait_until([&link] { return link->pings.load() >= 1; }));

    const auto asked = steady_clock::now();
    actor.request_stop();
    CHECK(steady_clock::now() - asked < milliseconds{100});

    /* The thread leaves only once the request it is inside returns. */
    CHECK(wait_until([&actor] { return actor.finished(); }));
    CHECK(steady_clock::now() - asked >= milliseconds{200});
}

TEST_CASE("watch changes and ticks interleave safely", "[market_stream]") {
    constexpr std::int64_t second = 1'000'000'000;
    tick_sink sink;
    sink.watch(fx_handle, canonical(fx_series));

    std::thread watcher([&sink]() {
        for (int i = 0; i < 2000; ++i) {
            sink.watch(swap_handle, canonical(swap_series));
            sink.unwatch(swap_handle);
        }
    });
    std::int64_t at = second;
    for (int i = 0; i < 2000; ++i, at += second) {
        sink.on_tick(tick_for(fx_series, at, 1.0));
        sink.on_tick(tick_for(swap_series, at, 2.0));
    }
    watcher.join();

    std::vector<series_bucket> buckets;
    REQUIRE(sink.drain(buckets) > 0);
    std::size_t fx = 0;
    for (const auto& bucket : buckets)
        fx += bucket.series == fx_handle;
    CHECK(fx >= 1999);
    CHECK(sink.ticks() >= 2000);
}
