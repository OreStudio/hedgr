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
#include "holder_audit.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/service/market_feed.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_bucket.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
using hedgr::model::domain::window_spec;
using hedgr::model::event::event;
using hedgr::model::event::event_kind;
using hedgr::model::state::series_bucket;
using hedgr::model::state::series_handle;
using hedgr::runtime::manager::instance_manager;
using hedgr::runtime::worker::session_actor_options;
using hedgr::runtime::worker::tick_feed;
using hedgr::runtime::worker::watch_target;
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
    session_actor_options options;
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

    void set_watch(std::span<const watch_target> wanted) override {
        for (const auto& held : watching_) {
            const bool kept = std::ranges::any_of(wanted,
                [&](const watch_target& target) {
                    return target.series == held.series;
                });
            if (!kept)
                state_->unwatches.emplace_back(held.series);
        }
        for (const auto& target : wanted) {
            const bool held = std::ranges::any_of(watching_,
                [&](const watch_target& current) {
                    return current.series == target.series;
                });
            if (!held)
                state_->watches.emplace_back(
                    target.series, target.canonical_uri);
        }
        watching_.assign(wanted.begin(), wanted.end());
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
    std::vector<watch_target> watching_;
};

/** @brief The manager, one unreachable instance, and every feed it made. */
struct world final {
    world() {
        make_manager();

        command save;
        save.kind = command_kind::save_instance;
        save.instance_row.id = instance_id;
        save.instance_row.name = "Refused";
        save.instance_row.nats_url = "nats://127.0.0.1:1";
        save.instance_row.subject_prefix = "ores.test.refused";
        save.instance_id = instance_id;
        push(std::move(save));
    }

    void make_manager() {
        manager = std::make_unique<instance_manager>(1,
            [this](session_actor_options options)
                -> std::unique_ptr<tick_feed> {
                auto state = std::make_shared<feed_state>();
                state->options = std::move(options);
                feeds.push_back(state);
                return std::make_unique<fake_feed>(state);
            });
        REQUIRE(manager->start(database.path(), seed_options()).has_value());
    }

    /**
     * @brief Stops the manager and starts another on the same store.
     *
     * The windows in @p extra are written to the store while no manager has it
     * open, so the new one restores them, which is how two windows can show
     * one series: the commands never make a second.
     */
    void restart(const std::vector<window_spec>& extra = {}) {
        manager->stop();
        manager.reset();
        if (!extra.empty()) {
            auto opened = hedgr::store::sqlite::database::open(database.path());
            REQUIRE(opened.has_value());
            hedgr::store::repository::window_repository windows{opened.value()};
            for (const auto& spec : extra)
                REQUIRE(windows.save(spec).has_value());
        }
        feeds.clear();
        make_manager();
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

    void open_mini_plot(const std::string& account_id,
        const std::string& series,
        std::optional<std::int64_t> span_ns = std::nullopt) {
        command open;
        open.series_span_ns = span_ns;
        open.kind = command_kind::open_window;
        open.instance_id = instance_id;
        open.account_id = account_id;
        open.new_window_kind = window_kind::mini_plot;
        open.series_id = series;
        open.series_label = series;
        open.party_id = party;
        push(std::move(open));
    }

    void close_mini_plot(const std::string& account_id,
        const std::string& series) {
        command close;
        close.kind = command_kind::close_window;
        close.window_id =
            std::format("mini:{}:{}:{}", instance_id, account_id, series);
        push(std::move(close));
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
 * read is never watched. The row names a quote, so the canonical series URI
 * differs from the row's. Each id gets its own currency, so two ids are two
 * series.
 */
[[nodiscard]] std::string series_uri(const std::string& id) {
    const std::map<std::string, std::string> currency{
        {"s-1", "USD"}, {"s-2", "EUR"}, {"s-far", "JPY"}};
    const auto found = currency.find(id);
    return "oresmd://fx/GBP?type=quote&instrument=fx_spot&quote=rate&ccy="
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

TEST_CASE("a catalogue event carries the provider kinds it was read with",
    "[feed][providers]") {
    world w;
    w.sign_in("a-1");

    event fact;
    fact.kind = event_kind::series_catalogue;
    fact.instance_id = instance_id;
    fact.account_id = "a-1";
    fact.series = {row("s-1")};
    fact.providers = {
        {.code = "ore", .name = "ORE Studio", .kind = "reference"},
        {.code = "bloomberg", .name = "Bloomberg", .kind = "data_vendor"}};
    w.push(std::move(fact));

    const auto* catalogue
        = w.manager->market().catalogue(instance_id, "a-1");
    REQUIRE(catalogue != nullptr);
    REQUIRE(catalogue->providers.size() == 2);
    CHECK(hedgr::model::domain::is_reference_provider(
        catalogue->providers[0]));
    CHECK(catalogue->providers[1].code == "bloomberg");
    CHECK(catalogue->providers[1].kind == "data_vendor");
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
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a chart whose series is not in the catalogue is not watched",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-unknown");
    CHECK(w.feeds.at(0)->watches.empty());
}

TEST_CASE("a chart whose series the catalogue does not name says so, not spins",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-unknown");

    const auto handle = w.handle_of("a-1", "s-unknown");
    CHECK_FALSE(w.manager->market().failure(handle).empty());
    CHECK(w.manager->market().failure(handle).find("catalogue")
        != std::string_view::npos);
    CHECK(w.manager->market().observed_at_ns(handle) != 0);
    CHECK(w.feeds.at(0)->watches.empty());
}

TEST_CASE("a restored chart whose series a later catalogue lacks fails, not spins",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.open_chart("a-1", "s-gone");
    REQUIRE(wait_until([&] {
        w.manager->pump();
        const auto* catalogue = w.manager->market().catalogue(instance_id, "a-1");
        return catalogue != nullptr
            && catalogue->state
                == hedgr::model::state::catalogue_state::failed;
    }));

    w.set_catalogue("a-1", {row("s-1")});

    const auto handle = w.handle_of("a-1", "s-gone");
    CHECK_FALSE(w.manager->market().failure(handle).empty());
    CHECK(w.manager->market().observed_at_ns(handle) != 0);
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

    /* The backfills fail on their own clock, and each failure stamps a row. */
    REQUIRE(wait_until([&] {
        w.manager->pump();
        return w.settled("a-1", "s-1") && w.settled("a-1", "s-2")
            && w.settled("a-2", "s-1");
    }));
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
    REQUIRE(wait_until([&] {
        w.manager->pump();
        return w.settled("a-1", "s-1") && w.settled("a-1", "s-2");
    }));
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

TEST_CASE("an open chart holds its series and closing it lets go", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    const auto series = w.handle_of("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);
    hedgr::testing::audit_holders(*w.manager);

    w.close_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 0);
    hedgr::testing::audit_holders(*w.manager);

    w.open_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);
    CHECK(w.handle_of("a-1", "s-1") == series);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a chart opened with no session holds nothing until its session is "
    "there", "[feed]") {
    world w;
    w.open_chart("a-1", "s-1");
    CHECK(w.manager->market().size() == 0);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a session that stops being authenticated keeps its charts and "
    "watches them again on the next stream", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    const auto series = w.handle_of("a-1", "s-1");
    REQUIRE(w.manager->market().watchers(series) == 1);

    /* Only the pump's sweep of unauthenticated sessions retires this stream. */
    event fact;
    fact.kind = event_kind::session_status_changed;
    fact.instance_id = instance_id;
    fact.account_id = "a-1";
    fact.status = session_status::disconnected;
    w.push(std::move(fact));
    CHECK(w.feeds.at(0)->stops == 1);
    CHECK(w.manager->market().watchers(series) == 1);
    hedgr::testing::audit_holders(*w.manager);

    w.tell(event_kind::session_authenticated, "a-1");
    REQUIRE(w.feeds.size() == 2);
    CHECK(watches(w, *w.feeds.at(1), "a-1", {"s-1"}));
    CHECK(w.feeds.at(1)->unwatches.empty());
    hedgr::testing::audit_holders(*w.manager);
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

TEST_CASE("acting as another party replaces the token and reopens the stream",
    "[feed][party]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    REQUIRE(w.feeds.size() == 1);

    event changed;
    changed.kind = event_kind::session_party_changed;
    changed.instance_id = instance_id;
    changed.account_id = "a-1";
    changed.tenant_id = "tenant-2";
    changed.tenant_name = "Tenant Two";
    changed.party_id = "p-2";
    changed.party_name = "Second";
    changed.token = secret_string{std::string{"token-2"}};
    changed.payload = "a-1@example.test";
    w.manager->events().push(std::move(changed));
    w.manager->pump();

    const auto handle = w.manager->sessions().find(instance_id, "a-1");
    REQUIRE(handle.has_value());
    CHECK(w.manager->sessions().party_id(*handle) == "p-2");
    CHECK(w.manager->sessions().party_name(*handle) == "Second");
    CHECK(w.manager->sessions().tenant_id(*handle) == "tenant-2");
    CHECK(w.manager->sessions().token(*handle) == "token-2");

    /*
     * The old stream belonged to the old party, so it is retired and another
     * opened against the same deployment with the new party and token.
     */
    REQUIRE(w.feeds.size() == 2);
    CHECK(w.feeds[0]->stops == 1);
    CHECK(w.feeds[1]->options.party_id == "p-2");
    CHECK(w.feeds[1]->options.token == "token-2");
}

TEST_CASE("a refused party leaves the session on the party it had", "[feed][party]") {
    world w;
    w.sign_in("a-1");

    event refused;
    refused.kind = event_kind::session_party_refused;
    refused.instance_id = instance_id;
    refused.account_id = "a-1";
    refused.message = "not a party of this login";
    w.manager->events().push(std::move(refused));
    w.manager->pump();

    const auto handle = w.manager->sessions().find(instance_id, "a-1");
    REQUIRE(handle.has_value());
    CHECK(w.manager->sessions().party_id(*handle) == party);
    CHECK(w.manager->sessions().status(*handle) == session_status::authenticated);
}

namespace {

[[nodiscard]] bool raised(const instance_manager& manager,
    std::string_view words) {
    for (const auto& row : manager.conditions().rows())
        if (row.key.source == hedgr::model::state::condition_source::session
            && row.message.find(words) != std::string::npos)
            return true;
    return false;
}

}

TEST_CASE("a party command without a party is refused", "[feed][party]") {
    world w;

    command request;
    request.kind = command_kind::select_party;
    request.instance_id = instance_id;
    request.account_id = "a-1";
    w.push(std::move(request));

    CHECK(raised(*w.manager, "no party was named"));
}

TEST_CASE("a party command on a session that is not signed in is refused",
    "[feed][party]") {
    world w;

    command request;
    request.kind = command_kind::select_party;
    request.instance_id = instance_id;
    request.account_id = "a-1";
    request.party_id = "p-2";
    w.push(std::move(request));

    CHECK(raised(*w.manager, "needs a signed-in session"));
}

TEST_CASE("a party command with no worker is refused", "[feed][party]") {
    world w;
    w.sign_in("a-1");

    /*
     * The sign-in above is a hand-pushed fact after a failed connect, so the
     * session row is authenticated while its worker has already finished. The
     * manager must not leave the control changed with nothing to apply it.
     */
    command request;
    request.kind = command_kind::select_party;
    request.instance_id = instance_id;
    request.account_id = "a-1";
    request.party_id = "p-2";
    w.push(std::move(request));

    CHECK(raised(*w.manager, "no worker"));
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
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
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
    hedgr::testing::audit_holders(*w.manager);
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

namespace {

/** @brief A chart window restored from the store, whatever it is called. */
[[nodiscard]] window_spec stored_chart(std::string window_id,
    const std::string& account_id, const std::string& series) {
    window_spec spec;
    spec.id = std::move(window_id);
    spec.kind = window_kind::chart;
    spec.session = hedgr::model::domain::session_key{instance_id, account_id};
    spec.state = hedgr::model::domain::chart_state{
        .label = series, .series_id = series};
    return spec;
}

[[nodiscard]] std::vector<market_series_row> rows_named(std::size_t count) {
    std::vector<market_series_row> rows;
    for (std::size_t i = 0; i < count; ++i)
        rows.push_back(row(std::format("c-{}", i)));
    return rows;
}

}

TEST_CASE("a pass over an idle desk calls nothing", "[feed][cost]") {
    world w;
    w.sign_in("a-1");
    constexpr std::size_t charts = 60;
    w.set_catalogue("a-1", rows_named(charts));
    for (std::size_t i = 0; i < charts; ++i)
        w.open_chart("a-1", std::format("c-{}", i));
    auto& feed = *w.feeds.at(0);
    REQUIRE(feed.watches.size() == charts);

    for (int frame = 0; frame < 20; ++frame)
        w.manager->pump();
    CHECK(feed.watches.size() == charts);
    CHECK(feed.unwatches.empty());
    hedgr::testing::audit_holders(*w.manager);
}

/**
 * @brief A feed on its own tables, with every chart held the way a window holds it.
 *
 * The feed reads tables and holds none of them, so this is the whole of its
 * input: a series table whose rows have watchers, and a session. Nothing else
 * in the manager's frame is in the count.
 */
struct bare_feed final {
    explicit bare_feed(std::size_t charts, std::size_t catalogue_rows) {
        hedgr::model::domain::instance row;
        row.id = instance_id;
        row.name = "Refused";
        row.nats_url = "nats://127.0.0.1:1";
        row.subject_prefix = "ores.test.refused";
        static_cast<void>(instances.upsert(row));

        session = sessions.insert(instance_id, "a-1");
        sessions.set_account_result(session, "tenant", "party", party,
            secret_string{std::string{"a-token"}});
        sessions.set_status(session, session_status::authenticated);

        auto rows = rows_named(catalogue_rows);
        series.apply_catalogue(instance_id, "a-1", std::move(rows));
        for (std::size_t i = 0; i < charts; ++i) {
            series.acquire(
                series.intern(instance_id, "a-1", std::format("c-{}", i)));
        }

        feed = std::make_unique<hedgr::runtime::service::market_feed>(
            instances, sessions, series, options, [this](session_actor_options) {
                return std::make_unique<fake_feed>(state);
            },
            [this](std::string_view, std::string_view)
                -> std::optional<hedgr::runtime::refusal> {
                ++catalogue_reads;
                return std::nullopt;
            },
            [this](std::string_view, std::string_view, std::string_view) {
                ++backfills;
            });
        feed->attach(std::filesystem::temp_directory_path() / "bare_feed.sqlite");
        event opened;
        opened.instance_id = instance_id;
        opened.account_id = "a-1";
        opened.party_id = party;
        opened.token = secret_string{std::string{"a-token"}};
        feed->open(opened);
    }

    hedgr::model::state::instance_table instances;
    hedgr::model::state::session_table sessions;
    hedgr::model::state::series_table series;
    hedgr::model::state::session_handle session;
    session_options options;
    std::shared_ptr<feed_state> state = std::make_shared<feed_state>();
    std::size_t catalogue_reads = 0;
    std::size_t backfills = 0;
    std::unique_ptr<hedgr::runtime::service::market_feed> feed;
};

TEST_CASE("the feed's pass over an idle desk allocates nothing whatever the "
    "desk holds", "[feed][cost]") {
    const auto idle_allocations = [](std::size_t charts, std::size_t rows) {
        bare_feed desk{charts, rows};
        REQUIRE(desk.feed->pump().empty());
        REQUIRE(desk.state->watches.size() == charts);
        REQUIRE(desk.backfills == charts);
        REQUIRE(desk.catalogue_reads == 0);
        static_cast<void>(desk.feed->pump());

        const auto before = hedgr::testing::allocations();
        for (int frame = 0; frame < 50; ++frame)
            static_cast<void>(desk.feed->pump());
        const auto spent = hedgr::testing::allocations() - before;
        CHECK(desk.state->watches.size() == charts);
        CHECK(desk.state->unwatches.empty());
        CHECK(desk.backfills == charts);
        return spent;
    };

    CHECK(idle_allocations(5, 20) == 0);
    CHECK(idle_allocations(200, 5000) == 0);
}

TEST_CASE("two charts of one series watch it once and the last one to close "
    "unwatches it", "[feed]") {
    world w;
    w.restart({stored_chart("w-a", "a-1", "s-1"),
        stored_chart("w-b", "a-1", "s-1")});

    /* Restored before any session signs in, each window already holds it. */
    const auto series = w.handle_of("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 2);
    hedgr::testing::audit_holders(*w.manager);

    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    auto& feed = *w.feeds.at(0);
    CHECK(watches(w, feed, "a-1", {"s-1"}));
    CHECK(feed.watches.size() == 1);

    command close;
    close.kind = command_kind::close_window;
    close.window_id = "w-a";
    w.push(close);
    CHECK(w.manager->market().watchers(series) == 1);
    CHECK(feed.unwatches.empty());
    CHECK(feed.watches.size() == 1);
    hedgr::testing::audit_holders(*w.manager);

    close.window_id = "w-b";
    w.push(close);
    CHECK(w.manager->market().watchers(series) == 0);
    REQUIRE(feed.unwatches.size() == 1);
    CHECK(feed.unwatches[0] == series);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a restored chart comes back live when its session signs in",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    REQUIRE(w.feeds.at(0)->watches.size() == 1);

    w.restart();
    const auto series = w.handle_of("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);
    CHECK(w.feeds.empty());
    hedgr::testing::audit_holders(*w.manager);

    /* The restored chart has no catalogue, so the feed reads one for it. */
    w.sign_in("a-1");
    REQUIRE(w.feeds.size() == 1);
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
    CHECK(w.manager->market().watchers(series) == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a catalogue that drops a watched series unwatches it and one that "
    "lists it again watches it", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1"), row("s-2")});
    w.open_chart("a-1", "s-1");
    w.open_chart("a-1", "s-2");
    auto& feed = *w.feeds.at(0);
    REQUIRE(feed.watches.size() == 2);
    const auto series = w.handle_of("a-1", "s-1");

    w.set_catalogue("a-1", {row("s-2")});
    REQUIRE(feed.unwatches.size() == 1);
    CHECK(feed.unwatches[0] == series);
    CHECK(w.manager->market().watchers(series) == 1);

    w.set_catalogue("a-1", {row("s-2")});
    CHECK(feed.unwatches.size() == 1);
    CHECK(feed.watches.size() == 2);

    w.set_catalogue("a-1", {row("s-1"), row("s-2")});
    CHECK(feed.watches.size() == 3);
    CHECK(feed.watches.back().first == series);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a catalogue that changes the party of a watched series stops "
    "watching it", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    auto& feed = *w.feeds.at(0);
    REQUIRE(feed.watches.size() == 1);

    w.set_catalogue("a-1", {row("s-1", "p-other")});
    CHECK(feed.unwatches.size() == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("removing an account gives back what its charts held and forgets "
    "its series", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.sign_in("a-2");
    w.set_catalogue("a-1", {row("s-1")});
    w.set_catalogue("a-2", {row("s-1")});
    w.open_chart("a-1", "s-1");
    w.open_chart("a-2", "s-1");
    const auto kept = w.handle_of("a-2", "s-1");
    REQUIRE(w.manager->market().size() == 2);

    command remove;
    remove.kind = command_kind::remove_account;
    remove.instance_id = instance_id;
    remove.account_id = "a-1";
    w.push(std::move(remove));

    const auto& series = w.manager->market();
    CHECK_FALSE(series.find(instance_id, "a-1", "s-1").has_value());
    CHECK(series.catalogue(instance_id, "a-1") == nullptr);
    CHECK(series.size() == 1);
    CHECK(series.watchers(kept) == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a chart whose account was removed and added back is live again",
    "[feed]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_chart("a-1", "s-1");
    REQUIRE(w.feeds.at(0)->watches.size() == 1);

    command remove;
    remove.kind = command_kind::remove_account;
    remove.instance_id = instance_id;
    remove.account_id = "a-1";
    w.push(std::move(remove));
    CHECK(w.manager->market().size() == 0);
    hedgr::testing::audit_holders(*w.manager);

    w.sign_in("a-1");
    REQUIRE(w.feeds.size() == 2);
    w.set_catalogue("a-1", {row("s-1")});
    CHECK(watches(w, *w.feeds.at(1), "a-1", {"s-1"}));
    CHECK(w.manager->market().watchers(w.handle_of("a-1", "s-1")) == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("removing an instance gives back what its charts held and forgets "
    "its series", "[feed]") {
    world w;
    w.sign_in("a-1");
    w.sign_in("a-2");
    w.set_catalogue("a-1", {row("s-1")});
    w.set_catalogue("a-2", {row("s-1"), row("s-2")});
    w.open_chart("a-1", "s-1");
    w.open_chart("a-2", "s-2");
    REQUIRE(w.manager->market().size() == 2);

    command remove;
    remove.kind = command_kind::remove_instance;
    remove.instance_id = instance_id;
    w.push(std::move(remove));

    const auto& series = w.manager->market();
    CHECK(series.size() == 0);
    CHECK(series.catalogue(instance_id, "a-1") == nullptr);
    CHECK(series.catalogue(instance_id, "a-2") == nullptr);
    CHECK(series.watcher_total() == 0);
    CHECK(w.feeds.at(0)->stops == 1);
    CHECK(w.feeds.at(1)->stops == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a mini plot holds its series while open and gives it back on close",
    "[feed][mini_plot]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    auto& feed = *w.feeds.at(0);

    w.open_mini_plot("a-1", "s-1");
    CHECK(watches(w, feed, "a-1", {"s-1"}));
    const auto series = w.handle_of("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);

    w.open_mini_plot("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);

    w.open_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 2);
    CHECK(feed.unwatches.empty());

    w.close_mini_plot("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 1);
    CHECK(feed.unwatches.empty());
    w.close_chart("a-1", "s-1");
    CHECK(w.manager->market().watchers(series) == 0);
    CHECK(feed.unwatches.size() == 1);
    hedgr::testing::audit_holders(*w.manager);
}

TEST_CASE("a mini plot opens on the span it was asked for, else the chart default",
    "[feed][mini_plot]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1"), row("s-2"), row("s-3")});

    const auto span_of = [&](const std::string& series) {
        const auto id = std::format("mini:{}:a-1:{}", instance_id, series);
        const auto handle = w.manager->windows().find(id);
        REQUIRE(handle.has_value());
        const auto* card = std::get_if<hedgr::model::domain::mini_plot_state>(
            &w.manager->windows().state(*handle));
        REQUIRE(card != nullptr);
        return card->span_ns;
    };
    w.open_mini_plot("a-1", "s-1", std::int64_t{15} * 60 * 1'000'000'000);
    w.open_mini_plot("a-1", "s-2", std::int64_t{0});
    w.open_mini_plot("a-1", "s-3");
    CHECK(span_of("s-1") == std::int64_t{15} * 60 * 1'000'000'000);
    CHECK(span_of("s-2") == 0);
    CHECK(span_of("s-3") == std::int64_t{5} * 60 * 1'000'000'000);
}

TEST_CASE("removing the account gives back what its mini plots held",
    "[feed][mini_plot]") {
    world w;
    w.sign_in("a-1");
    w.set_catalogue("a-1", {row("s-1")});
    w.open_mini_plot("a-1", "s-1");
    REQUIRE(w.manager->market().watchers(w.handle_of("a-1", "s-1")) == 1);

    command remove;
    remove.kind = command_kind::remove_account;
    remove.instance_id = instance_id;
    remove.account_id = "a-1";
    w.push(std::move(remove));

    CHECK(w.manager->market().size() == 0);
    hedgr::testing::audit_holders(*w.manager);
}
