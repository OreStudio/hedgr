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
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::instance;
using hedgr::model::domain::market_level;
using hedgr::model::domain::market_series_row;
using hedgr::model::domain::secret_string;
using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
using hedgr::model::state::series_bucket;
using hedgr::model::state::series_handle;
using hedgr::runtime::manager::instance_manager;
using hedgr::runtime::worker::market_stream_options;
using hedgr::runtime::worker::tick_feed;
using hedgr::session::transport::session_options;

constexpr auto instance_id = "inst-refused";
constexpr auto party = "p-1";
constexpr auto tenant = "tenant-1";

/** @brief A database file that deletes itself, sidecars included. */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.feed.{}.{}.sqlite", unique_token(),
                  next_serial())) {
        erase();
    }

    ~temporary_database() { erase(); }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    void erase() const {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    std::filesystem::path path_;
};

[[nodiscard]] session_options seed_options() {
    session_options options;
    options.nats_url = "nats://localhost:20405";
    options.subject_prefix = "ores.dev.brave_hopper";
    return options;
}

[[nodiscard]] bool wait_until(const std::function<bool()>& ready,
    std::chrono::milliseconds limit = std::chrono::seconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

/**
 * @brief What a fake feed saw and what a test tells it to do.
 *
 * Shared with the test, because the manager owns the feed and destroys it when
 * it forgets it. Everything here is touched on the frame thread only.
 */
struct feed_state final {
    market_stream_options options;
    std::vector<std::pair<series_handle, std::string>> watches;
    std::vector<series_handle> unwatches;
    std::vector<std::string> tokens;
    int stops = 0;
    bool finished = false;
    bool reconnected = false;
    bool destroyed = false;
    std::vector<series_bucket> waiting;
};

class fake_feed final : public tick_feed {
public:
    explicit fake_feed(std::shared_ptr<feed_state> state)
        : state_(std::move(state)) {}

    ~fake_feed() override { state_->destroyed = true; }

    void request_stop() override { ++state_->stops; }

    [[nodiscard]] bool finished() const noexcept override {
        return state_->finished;
    }

    void set_token(std::string token) override {
        state_->tokens.push_back(std::move(token));
    }

    void watch(series_handle series, std::string canonical_uri) override {
        state_->watches.emplace_back(series, std::move(canonical_uri));
    }

    void unwatch(series_handle series) override {
        state_->unwatches.emplace_back(series);
    }

    std::size_t drain(std::vector<series_bucket>& out) override {
        out.clear();
        out.swap(state_->waiting);
        return out.size();
    }

    bool take_reconnected() override {
        return std::exchange(state_->reconnected, false);
    }

private:
    std::shared_ptr<feed_state> state_;
};

/** @brief The manager, one unreachable instance, and every feed it made. */
struct world final {
    world() {
        manager = std::make_unique<instance_manager>(1,
            [this](market_stream_options options)
                -> std::unique_ptr<tick_feed> {
                auto state = std::make_shared<feed_state>();
                state->options = std::move(options);
                feeds.push_back(state);
                return std::make_unique<fake_feed>(state);
            });
        REQUIRE(manager->start(database.path(), seed_options()).has_value());

        command save;
        save.kind = command_kind::save_instance;
        save.instance_row.id = instance_id;
        save.instance_row.name = "Refused";
        save.instance_row.nats_url = "nats://127.0.0.1:1";
        save.instance_row.subject_prefix = "ores.test.refused";
        save.instance_id = instance_id;
        push(std::move(save));
    }

    ~world() { manager->stop(); }

    void push(command request) {
        manager->commands().push(std::move(request));
        manager->pump();
    }

    void push(event fact) {
        manager->events().push(std::move(fact));
        manager->pump();
    }

    /**
     * @brief Signs @p account_id in as far as the frame loop can tell.
     *
     * The real attempt is refused first so the session row exists, and the
     * authenticated fact is pushed by hand afterwards, as the read tests do.
     */
    void sign_in(const std::string& account_id,
        std::string token = "token-1") {
        account row;
        row.id = account_id;
        row.instance_id = instance_id;
        row.name = "ops";
        row.principal = account_id + "@example.test";
        command save;
        save.kind = command_kind::save_account;
        save.account_row = row;
        push(std::move(save));

        command connect;
        connect.kind = command_kind::connect_session;
        connect.instance_id = instance_id;
        connect.account_id = account_id;
        connect.principal = account_id + "@example.test";
        connect.password = secret_string{std::string{"not-a-real-secret"}};
        push(std::move(connect));
        REQUIRE(wait_until([&] {
            manager->pump();
            const auto handle = manager->sessions().find(instance_id, account_id);
            return handle
                && manager->sessions().status(*handle) == session_status::failed;
        }));

        event fact;
        fact.kind = event_kind::session_authenticated;
        fact.instance_id = instance_id;
        fact.account_id = account_id;
        fact.tenant_id = tenant;
        fact.party_id = party;
        fact.payload = account_id + "@example.test";
        fact.token = secret_string{std::move(token)};
        push(std::move(fact));
    }

    void set_catalogue(const std::string& account_id,
        std::vector<market_series_row> rows) {
        event fact;
        fact.kind = event_kind::series_catalogue;
        fact.instance_id = instance_id;
        fact.account_id = account_id;
        fact.series = std::move(rows);
        push(std::move(fact));
    }

    void open_chart(const std::string& account_id, const std::string& series) {
        command open;
        open.kind = command_kind::open_window;
        open.instance_id = instance_id;
        open.account_id = account_id;
        open.new_window_kind = window_kind::chart;
        open.series_id = series;
        open.series_label = series;
        open.party_id = party;
        push(std::move(open));
    }

    void close_chart(const std::string& account_id, const std::string& series) {
        command close;
        close.kind = command_kind::close_window;
        close.window_id =
            std::format("chart:{}:{}:{}", instance_id, account_id, series);
        push(std::move(close));
    }

    /** @brief Sends @p kind for @p account_id, as the worker would. */
    void tell(event_kind kind, const std::string& account_id,
        std::uint64_t attempt = 0) {
        event fact;
        fact.kind = kind;
        fact.instance_id = instance_id;
        fact.account_id = account_id;
        fact.tenant_id = tenant;
        fact.party_id = party;
        fact.attempt = attempt;
        fact.message = "the connection dropped";
        fact.token = secret_string{std::string{"token-2"}};
        push(std::move(fact));
    }

    [[nodiscard]] bool settled(const std::string& account_id,
        const std::string& series) const {
        const auto held =
            manager->market().find(instance_id, account_id, series);
        return held && !manager->market().failure(*held).empty();
    }

    /** @brief The handle the table holds for a series of this world. */
    [[nodiscard]] series_handle handle_of(const std::string& account_id,
        const std::string& series) const {
        const auto held =
            manager->market().find(instance_id, account_id, series);
        REQUIRE(held.has_value());
        return *held;
    }

    temporary_database database;
    std::vector<std::shared_ptr<feed_state>> feeds;
    std::unique_ptr<instance_manager> manager;
};

/**
 * @brief A catalogue row whose URI names a real series.
 *
 * The feed canonicalises the URI before a watch, so a URI the codec does not
 * read is never watched. Each id gets its own currency, so two ids are two
 * series.
 */
[[nodiscard]] std::string series_uri(const std::string& id) {
    const std::map<std::string, std::string> currency{
        {"s-1", "USD"}, {"s-2", "EUR"}, {"s-far", "JPY"}};
    const auto found = currency.find(id);
    return "oresmd://fx/GBP?type=series&instrument=fx_spot&quote=rate&ccy="
        + (found == currency.end() ? std::string{"CHF"} : found->second);
}

[[nodiscard]] std::string canonical(const std::string& id) {
    const auto key = hedgr::session::market::canonical_series_uri(series_uri(id));
    REQUIRE(key.has_value());
    return *key;
}

[[nodiscard]] market_series_row row(std::string id, std::string party_id = party) {
    market_series_row series;
    series.uri = series_uri(id);
    series.id = std::move(id);
    series.party_id = std::move(party_id);
    return series;
}

/** @brief Whether the feed watched exactly the series in @p expected. */
[[nodiscard]] bool watches(const world& w, const feed_state& feed,
    const std::string& account_id, std::vector<std::string> expected) {
    using key = std::pair<std::uint32_t, std::uint32_t>;
    std::vector<key> seen;
    for (const auto& [series, uri] : feed.watches)
        seen.emplace_back(series.index, series.generation);
    std::vector<key> wanted;
    for (const auto& id : expected) {
        const auto series = w.handle_of(account_id, id);
        wanted.emplace_back(series.index, series.generation);
    }
    std::ranges::sort(seen);
    std::ranges::sort(wanted);
    return seen == wanted;
}

}

TEST_CASE("signing in opens one stream with the session's credentials",
    "[feed]") {
    world w;
    w.sign_in("a-1", "token-1");

    REQUIRE(w.feeds.size() == 1);
    const auto& options = w.feeds[0]->options;
    CHECK(options.token == "token-1");
    CHECK(options.principal == "a-1@example.test");
    CHECK(options.tenant_id == tenant);
    CHECK(options.party_id == party);
    CHECK(options.options.nats_url == "nats://127.0.0.1:1");
    CHECK(w.feeds[0]->stops == 0);

    w.tell(event_kind::session_authenticated, "a-1");
    REQUIRE(w.feeds.size() == 2);
    CHECK(w.feeds[0]->stops == 1);
    CHECK(w.feeds[1]->stops == 0);
    CHECK(w.feeds[1]->options.token == "token-2");
}

TEST_CASE("the watch set follows the open and closed charts", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1"), row("s-2"), row("s-far", "p-other")});
    auto& feed = *w.feeds.at(0);

    w.open_chart("a-1", "s-1");
    CHECK(watches(w, feed, "a-1", {"s-1"}));
    CHECK(feed.watches[0].second == canonical("s-1"));

    w.open_chart("a-1", "s-2");
    w.open_chart("a-1", "s-far");
    CHECK(watches(w, feed, "a-1", {"s-1", "s-2"}));

    w.manager->pump();
    CHECK(watches(w, feed, "a-1", {"s-1", "s-2"}));
    CHECK(feed.unwatches.empty());

    w.close_chart("a-1", "s-1");
    REQUIRE(feed.unwatches.size() == 1);
    CHECK(feed.unwatches[0] == w.handle_of("a-1", "s-1"));

    w.close_chart("a-1", "s-2");
    CHECK(feed.unwatches.size() == 2);

    w.open_chart("a-1", "s-1");
    CHECK(feed.watches.size() == 3);
}

TEST_CASE("a chart whose series is not in the catalogue is not watched",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-unknown");
    CHECK(w.feeds.at(0)->watches.empty());
}

TEST_CASE("a chart opened before the catalogue is read it, then is watched",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    REQUIRE(w.manager->market().catalogue(instance_id, "a-1") == nullptr);

    w.open_chart("a-1", "s-1");
    REQUIRE(wait_until([&] {
        w.manager->pump();
        const auto* catalogue = w.manager->market().catalogue(instance_id, "a-1");
        return catalogue != nullptr
            && catalogue->state
                == hedgr::model::state::catalogue_state::failed;
    }));
    CHECK(w.feeds.at(0)->watches.empty());

    w.set_catalogue("a-1", {row("s-1")});
    CHECK(watches(w, *w.feeds.at(0), "a-1", {"s-1"}));
}

TEST_CASE("watching a series seeds its history by a backfill", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    REQUIRE_FALSE(
        w.manager->market().find(instance_id, "a-1", "s-1").has_value());

    w.open_chart("a-1", "s-1");
    CHECK(wait_until([&] {
        w.manager->pump();
        return w.settled("a-1", "s-1");
    }));
}

TEST_CASE("a reconnect seeds every watched series again", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    REQUIRE(wait_until([&] {
        w.manager->pump();
        return w.settled("a-1", "s-1");
    }));
    const auto series = w.handle_of("a-1", "s-1");
    const auto first = w.manager->market().version(series);

    w.feeds.at(0)->reconnected = true;
    w.manager->pump();
    CHECK(wait_until([&] {
        w.manager->pump();
        return w.manager->market().version(series) != first;
    }));
    CHECK(w.feeds.at(0)->watches.size() == 1);
}

TEST_CASE("a drained bucket lands in the series it names", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.sign_in("a-2");
    REQUIRE(w.feeds.size() == 2);
    w.set_catalogue("a-1", {row("s-1"), row("s-2")});
    w.set_catalogue("a-2", {row("s-1")});
    w.open_chart("a-1", "s-1");
    w.open_chart("a-1", "s-2");
    w.open_chart("a-2", "s-1");

    const auto mine = w.handle_of("a-1", "s-1");
    const auto sibling = w.handle_of("a-1", "s-2");
    const auto other_session = w.handle_of("a-2", "s-1");
    const auto before = w.manager->market().version(sibling);

    series_bucket bucket;
    bucket.series = mine;
    bucket.level = market_level::medium;
    bucket.at_ns = 60'000'000'000;
    bucket.value = 1.25;
    bucket.open = 1.0;
    bucket.high = 1.5;
    bucket.low = 0.75;
    w.feeds[0]->waiting.push_back(bucket);
    w.manager->pump();

    const auto& series = w.manager->market();
    const auto medium = series.columns(mine, market_level::medium);
    REQUIRE(medium.size == 1);
    CHECK(medium.at_ns[0] == 60'000'000'000);
    CHECK(medium.value[0] == 1.25);
    CHECK(medium.open[0] == 1.0);
    CHECK(medium.high[0] == 1.5);
    CHECK(medium.low[0] == 0.75);
    CHECK(series.columns(mine, market_level::fine).size == 0);
    CHECK(series.columns(mine, market_level::coarse).size == 0);
    CHECK(series.columns(sibling, market_level::medium).size == 0);
    CHECK(series.version(sibling) == before);
    CHECK(series.columns(other_session, market_level::medium).size == 0);
    CHECK(w.feeds[0]->waiting.empty());
    CHECK(w.manager->stale_buckets() == 0);
    CHECK(w.manager->late_buckets() == 0);
}

TEST_CASE("a bucket whose series was released does not write the next "
    "occupant of its slot", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1"), row("s-2")});
    w.open_chart("a-1", "s-1");
    w.open_chart("a-1", "s-2");
    const auto live = w.handle_of("a-1", "s-2");
    const auto& series = w.manager->market();
    const auto before = series.version(live);

    /*
     * The handle a released series would have held: its slot, and the
     * generation before the one the occupant now carries.
     */
    series_bucket stale;
    stale.series = series_handle{live.index, live.generation - 1};
    stale.level = market_level::fine;
    stale.at_ns = 5'000'000'000;
    stale.value = 9.0;
    series_bucket nowhere = stale;
    nowhere.series = series_handle{9999, 1};
    w.feeds[0]->waiting.push_back(stale);
    w.feeds[0]->waiting.push_back(nowhere);
    w.manager->pump();

    CHECK(w.manager->stale_buckets() == 2);
    CHECK(w.manager->late_buckets() == 0);
    CHECK(series.columns(live, market_level::fine).size == 0);
    CHECK(series.version(live) == before);

    /* The occupant's own bucket is applied, and an older one counts as late. */
    series_bucket own = stale;
    own.series = live;
    w.feeds[0]->waiting.push_back(own);
    own.at_ns = 4'000'000'000;
    w.feeds[0]->waiting.push_back(own);
    w.manager->pump();
    CHECK(series.columns(live, market_level::fine).size == 1);
    CHECK(series.columns(live, market_level::fine).value[0] == 9.0);
    CHECK(w.manager->stale_buckets() == 2);
    CHECK(w.manager->late_buckets() == 1);
}

TEST_CASE("a series whose URI names no series is interned and backfilled but "
    "not watched", "[feed]") {
    world w;
    w.sign_in("a-1");
    auto odd = row("s-1");
    odd.uri = "not-a-uri";
    w.set_catalogue("a-1", {odd});
    w.open_chart("a-1", "s-1");

    CHECK(w.feeds.at(0)->watches.empty());
    CHECK(w.manager->market().find(instance_id, "a-1", "s-1").has_value());
    CHECK(wait_until([&] {
        w.manager->pump();
        return w.settled("a-1", "s-1");
    }));
}

TEST_CASE("a watch holds its series and unwatching lets it go", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    const auto series = w.handle_of("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);

    w.close_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 0);

    w.open_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);
    CHECK(w.handle_of("a-1", "s-1") == series);

    w.tell(event_kind::session_failed, "a-1");
    CHECK(w.manager->market().watchers(series) == 0);
}

TEST_CASE("a token refresh keeps the stream and its watches", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    auto& feed = *w.feeds.at(0);
    REQUIRE(feed.watches.size() == 1);

    w.tell(event_kind::session_token_refreshed, "a-1");

    CHECK(w.feeds.size() == 1);
    REQUIRE(feed.tokens.size() == 1);
    CHECK(feed.tokens[0] == "token-2");
    CHECK(feed.stops == 0);
    CHECK(feed.watches.size() == 1);
    CHECK(feed.unwatches.empty());
}

TEST_CASE("a refresh that follows a failure in one frame opens a stream the "
    "same frame retires", "[feed]") {
    world w;
    w.sign_in("a-1");

    event failed;
    failed.kind = event_kind::session_failed;
    failed.instance_id = instance_id;
    failed.account_id = "a-1";
    failed.message = "the connection dropped";
    event refreshed;
    refreshed.kind = event_kind::session_token_refreshed;
    refreshed.instance_id = instance_id;
    refreshed.account_id = "a-1";
    refreshed.token = secret_string{std::string{"token-2"}};
    w.manager->events().push(std::move(failed));
    w.manager->events().push(std::move(refreshed));
    w.manager->pump();

    REQUIRE(w.feeds.size() == 2);
    CHECK(w.feeds[0]->stops == 1);
    CHECK(w.feeds[0]->tokens.empty());
    CHECK(w.feeds[1]->options.token == "token-2");
    CHECK(w.feeds[1]->stops == 1);
}

TEST_CASE("a session that is lost retires its stream", "[feed]") {
    world w;
    w.sign_in("a-1");
    auto& feed = *w.feeds.at(0);

    SECTION("a failure") {
        w.tell(event_kind::session_failed, "a-1");
    }
    SECTION("a status that is no longer authenticated") {
        event fact;
        fact.kind = event_kind::session_status_changed;
        fact.instance_id = instance_id;
        fact.account_id = "a-1";
        fact.status = session_status::disconnected;
        w.push(std::move(fact));
    }
    SECTION("a disconnect") {
        command disconnect;
        disconnect.kind = command_kind::disconnect_session;
        disconnect.instance_id = instance_id;
        disconnect.account_id = "a-1";
        w.push(std::move(disconnect));
    }
    SECTION("the account being removed") {
        command remove;
        remove.kind = command_kind::remove_account;
        remove.instance_id = instance_id;
        remove.account_id = "a-1";
        w.push(std::move(remove));
    }

    CHECK(feed.stops == 1);
    w.manager->pump();
    CHECK(feed.stops == 1);
}

TEST_CASE("a retired stream is kept until it says it has finished", "[feed]") {
    world w;
    w.sign_in("a-1");
    auto& feed = *w.feeds.at(0);
    w.tell(event_kind::session_failed, "a-1");
    REQUIRE(feed.stops == 1);

    w.manager->pump();
    CHECK_FALSE(feed.destroyed);

    feed.finished = true;
    w.manager->pump();
    CHECK(feed.destroyed);
}

TEST_CASE("stopping the manager asks every stream to stop", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.sign_in("a-2");
    REQUIRE(w.feeds.size() == 2);

    w.manager->stop();
    CHECK(w.feeds[0]->stops == 1);
    CHECK(w.feeds[1]->stops == 1);
}

TEST_CASE("a fact from a replaced attempt does not touch the stream",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    auto& feed = *w.feeds.at(0);
    constexpr std::uint64_t stale = 1'000'000;

    w.tell(event_kind::session_failed, "a-1", stale);
    CHECK(feed.stops == 0);
    w.tell(event_kind::session_token_refreshed, "a-1", stale);
    CHECK(feed.tokens.empty());
    w.tell(event_kind::session_authenticated, "a-1", stale);
    CHECK(w.feeds.size() == 1);

    const auto handle = w.manager->sessions().find(instance_id, "a-1");
    REQUIRE(handle.has_value());
    CHECK(w.manager->sessions().status(*handle) == session_status::authenticated);
}
