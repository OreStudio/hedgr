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
#include "hedgr.model/state/series_bucket.hpp"
#include "hedgr.model/state/series_ring.hpp"
#include "hedgr.model/state/series_table.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/*
 * The series table is checked through its public interface only. The ring tests
 * compare all six columns, because a mutation that skips one column survives a
 * test that reads another.
 */

namespace {

using hedgr::model::domain::market_level;
using hedgr::model::domain::market_point;
using hedgr::model::domain::market_series_row;
using hedgr::model::state::append_outcome;
using hedgr::model::state::append_result;
using hedgr::model::state::series_columns;
using hedgr::model::state::series_point;
using hedgr::model::state::series_ring;
using hedgr::model::state::series_table;
using hedgr::testing::allocations;

constexpr std::int64_t second = 1'000'000'000;

/** A point whose six columns all differ, so a column that is skipped shows. */
series_point pt(std::int64_t sec, double value) {
    return {.at_ns = sec * second, .value = value, .open = value + 0.25,
        .high = value + 0.5, .low = value - 0.75};
}

series_point nth(std::size_t i) {
    return pt(static_cast<std::int64_t>(i) + 1, static_cast<double>(i) * 1.5);
}

void check_columns(const series_columns& cols,
    const std::vector<series_point>& expected, std::uint64_t first_seq) {
    REQUIRE(cols.size == expected.size());
    CHECK(cols.first_seq == first_seq);
    if (expected.empty())
        return;
    REQUIRE(cols.at_s != nullptr);
    REQUIRE(cols.value != nullptr);
    REQUIRE(cols.at_ns != nullptr);
    REQUIRE(cols.open != nullptr);
    REQUIRE(cols.high != nullptr);
    REQUIRE(cols.low != nullptr);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("index " << i);
        REQUIRE(cols.at_ns[i] == expected[i].at_ns);
        REQUIRE(cols.at_s[i] ==
            static_cast<double>(expected[i].at_ns) / 1e9);
        REQUIRE(cols.value[i] == expected[i].value);
        REQUIRE(cols.open[i] == expected[i].open);
        REQUIRE(cols.high[i] == expected[i].high);
        REQUIRE(cols.low[i] == expected[i].low);
    }
}

std::vector<series_point> newest(const std::vector<series_point>& all,
    std::size_t count) {
    const auto keep = std::min(count, all.size());
    return {all.end() - static_cast<std::ptrdiff_t>(keep), all.end()};
}

market_series_row row(std::string id, std::string uri = {},
    std::string party = {}) {
    market_series_row r;
    r.id = std::move(id);
    r.uri = std::move(uri);
    r.party_id = std::move(party);
    return r;
}

const std::string instance_id = "i-1";
const std::string account_id = "a-1";

}

TEST_CASE("a bucket is flat and a ring is empty until its first point",
    "[series]") {
    static_assert(sizeof(hedgr::model::state::series_bucket) == 56);
    series_ring ring{8};
    CHECK(ring.size() == 0);
    CHECK(ring.bytes() == 0);
    CHECK(ring.columns().value == nullptr);
    CHECK(ring.newest_at_ns() == 0);
}

TEST_CASE("the ring keeps the newest points in order across compaction",
    "[series][ring]") {
    /* Capacity 8 has a slack of 2, so the buffer fills at 10 and compacts. */
    series_ring ring{8};
    std::vector<series_point> all;
    for (std::size_t i = 0; i < 60; ++i) {
        all.push_back(nth(i));
        REQUIRE(ring.append(nth(i)) == append_outcome::appended);
        const auto kept = newest(all, 8);
        check_columns(ring.columns(), kept, all.size() - kept.size());
    }
}

TEST_CASE("a compaction at the real capacity drops the oldest points",
    "[series][ring]") {
    const auto capacity = hedgr::model::domain::market_fine_capacity;
    series_ring ring{capacity};
    std::vector<series_point> all;
    /* Past capacity plus slack by a few points, so one compaction happened. */
    const auto count = capacity + capacity / 4 + 5;
    for (std::size_t i = 0; i < count; ++i) {
        all.push_back(nth(i));
        ring.append(nth(i));
    }
    const auto kept = newest(all, capacity);
    check_columns(ring.columns(), kept, count - capacity);
}

TEST_CASE("a point older than the tail is dropped and an equal one replaces it",
    "[series][ring]") {
    series_ring ring{8};
    CHECK(ring.append(pt(5, 1.0)) == append_outcome::appended);
    CHECK(ring.append(pt(7, 2.0)) == append_outcome::appended);

    CHECK(ring.append(pt(6, 9.0)) == append_outcome::late);
    check_columns(ring.columns(), {pt(5, 1.0), pt(7, 2.0)}, 0);

    CHECK(ring.append(pt(7, 3.0)) == append_outcome::replaced);
    check_columns(ring.columns(), {pt(5, 1.0), pt(7, 3.0)}, 0);

    CHECK(ring.append(pt(8, 4.0)) == append_outcome::appended);
    check_columns(ring.columns(), {pt(5, 1.0), pt(7, 3.0), pt(8, 4.0)}, 0);
}

TEST_CASE("a point is allocated only while a buffer first grows",
    "[series][ring][cost]") {
    const auto capacity = hedgr::model::domain::market_fine_capacity;
    series_ring ring{capacity};
    const auto before = allocations();
    const auto full = capacity + capacity / 4;
    for (std::size_t i = 0; i < full; ++i)
        ring.append(nth(i));
    const auto growing = allocations() - before;
    /* Six buffers, doubling from a small start: a dozen steps at most. */
    CHECK(growing <= 6 * 12);
    CHECK(growing > 0);

    const auto steady = allocations();
    for (std::size_t i = full; i < 4 * full; ++i)
        ring.append(nth(i));
    CHECK(allocations() - steady == 0);
    CHECK(ring.size() == capacity);
    CHECK(ring.bytes() == full * 48);
}

TEST_CASE("a merge keeps held points on a shared bucket and fills the gaps",
    "[series][ring]") {
    series_ring ring{8};
    ring.append(pt(10, 1.0));
    ring.append(pt(12, 2.0));
    /* Buckets are 5 seconds wide: 5 holds 7, 10 holds 12 and 10, 15 holds 11. */
    const std::vector<series_point> seeded{
        pt(7, 9.0), pt(11, 9.5), pt(16, 8.0)};
    CHECK(ring.merge(seeded, 5 * second));
    check_columns(ring.columns(),
        {pt(7, 9.0), pt(10, 1.0), pt(12, 2.0), pt(16, 8.0)}, 2);
    CHECK_FALSE(ring.merge(seeded, 5 * second));
}

TEST_CASE("a merge keeps the newest capacity points", "[series][ring]") {
    series_ring ring{4};
    for (std::int64_t s = 10; s < 13; ++s)
        ring.append(pt(s, static_cast<double>(s)));
    std::vector<series_point> seeded;
    for (std::int64_t s = 1; s < 9; ++s)
        seeded.push_back(pt(s, -static_cast<double>(s)));
    CHECK(ring.merge(seeded, second));
    check_columns(ring.columns(),
        {pt(8, -8.0), pt(10, 10.0), pt(11, 11.0), pt(12, 12.0)}, 3);
}

TEST_CASE("every applied change stamps a value no other series held",
    "[series][stamp]") {
    series_table table;
    const auto one = table.intern(instance_id, account_id, "s-1");
    const auto two = table.intern(instance_id, account_id, "s-2");
    CHECK(table.version(one) == 0);

    CHECK(table.append(one, market_level::fine, pt(1, 1.1), 1) ==
        append_result::applied);
    const auto first = table.version(one);
    CHECK(first != 0);

    CHECK(table.append(two, market_level::fine, pt(1, 1.1), 1) ==
        append_result::applied);
    const auto other = table.version(two);
    CHECK(other != first);
    CHECK(table.version(one) == first);

    CHECK(table.append(one, market_level::fine, pt(2, 1.2), 2) ==
        append_result::applied);
    const auto appended = table.version(one);
    CHECK(appended != first);
    CHECK(appended != other);

    /* A replaced tail changes no size and no time, only the value. */
    CHECK(table.append(one, market_level::fine, pt(2, 1.3), 3) ==
        append_result::applied);
    const auto replaced = table.version(one);
    CHECK(replaced != appended);
    CHECK(replaced > other);

    table.fail(one, "no responder", 4);
    const auto failed = table.version(one);
    CHECK(failed > replaced);

    table.clear();
    const auto after = table.intern(instance_id, account_id, "s-1");
    table.append(after, market_level::fine, pt(1, 1.1), 1);
    CHECK(table.version(after) > failed);
}

TEST_CASE("a late bucket and an unchanged refresh stamp nothing",
    "[series][stamp]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    table.append(h, market_level::fine, pt(5, 1.0), 10);
    const auto stamp = table.version(h);

    CHECK(table.append(h, market_level::fine, pt(4, 9.0), 20) ==
        append_result::late);
    CHECK(table.version(h) == stamp);
    check_columns(table.columns(h, market_level::fine), {pt(5, 1.0)}, 0);
    CHECK(table.observed_at_ns(h) == 20);

    table.apply_catalogue(instance_id, account_id, {row("s-1", "u", "p")});
    const auto refreshed = table.version(h);
    table.apply_catalogue(instance_id, account_id, {row("s-1", "u", "p")});
    CHECK(table.version(h) == refreshed);
    CHECK(refreshed == stamp);
}

TEST_CASE("a late bucket that clears a failure stamps the row",
    "[series][stamp]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    table.append(h, market_level::fine, pt(5, 1.0), 10);
    table.fail(h, "no responder", 11);
    const auto failed = table.version(h);
    CHECK(table.failure(h) == "no responder");

    CHECK(table.append(h, market_level::fine, pt(4, 9.0), 12) ==
        append_result::late);
    CHECK(table.failure(h).empty());
    CHECK(table.version(h) > failed);
}

TEST_CASE("a released handle is stale and its slot gets a new generation",
    "[series][stale]") {
    series_table table;
    const auto old = table.intern(instance_id, account_id, "s-1");
    table.append(old, market_level::fine, pt(1, 1.0), 1);
    CHECK(table.contains(old));

    table.erase(old);
    CHECK_FALSE(table.contains(old));
    CHECK_FALSE(table.find(instance_id, account_id, "s-1").has_value());
    CHECK(table.version(old) == 0);
    CHECK(table.columns(old, market_level::fine).size == 0);
    CHECK(table.size() == 0);

    const auto fresh = table.intern(instance_id, account_id, "s-2");
    CHECK(fresh.index == old.index);
    CHECK(fresh.generation != old.generation);
    CHECK(fresh != old);
    table.append(fresh, market_level::fine, pt(7, 7.0), 1);
    const auto stamp = table.version(fresh);

    CHECK(table.append(old, market_level::fine, pt(8, 8.0), 2) ==
        append_result::stale);
    CHECK(table.seed(old, std::vector<market_point>{{.at_ns = second,
        .value = 1.0}}, 3) == false);
    table.fail(old, "late", 3);
    table.acquire(old);
    CHECK(table.version(fresh) == stamp);
    CHECK(table.failure(fresh).empty());
    CHECK(table.watchers(fresh) == 0);
    check_columns(table.columns(fresh, market_level::fine), {pt(7, 7.0)}, 0);
}

TEST_CASE("interning is idempotent and finds the row by its key",
    "[series][catalogue]") {
    series_table table;
    CHECK_FALSE(table.find(instance_id, account_id, "s-1").has_value());
    const auto h = table.intern(instance_id, account_id, "s-1");
    CHECK(table.intern(instance_id, account_id, "s-1") == h);
    CHECK(table.find(instance_id, account_id, "s-1") == h);
    CHECK(table.intern(instance_id, "a-2", "s-1") != h);
    CHECK(table.find(instance_id, "a-2", "s-1").has_value());
    CHECK(table.size() == 2);
}

TEST_CASE("a backfill merges by bucket and the live point wins",
    "[series][seed]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    /* A live fine bucket at 10.2 s. */
    table.append(h, market_level::fine,
        {.at_ns = 10 * second + 200'000'000, .value = 1.0, .open = 1.0,
         .high = 1.0, .low = 1.0}, 1);

    /* The read saw 9.5 s and a different price in the same second as 10.2. */
    const std::vector<market_point> observed{
        {.at_ns = 9 * second + 500'000'000, .value = 5.0},
        {.at_ns = 10 * second + 700'000'000, .value = 9.0}};
    CHECK(table.seed(h, observed, 2));

    const auto fine = table.columns(h, market_level::fine);
    REQUIRE(fine.size == 2);
    CHECK(fine.at_ns[0] == 9 * second + 500'000'000);
    CHECK(fine.value[0] == 5.0);
    CHECK(fine.at_ns[1] == 10 * second + 200'000'000);
    CHECK(fine.value[1] == 1.0);
}

TEST_CASE("a backfill fills all three levels and collapses by bucket",
    "[series][seed]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    const std::vector<market_point> observed{
        {.at_ns = 60 * second + 1, .value = 2.0},
        {.at_ns = 60 * second + 2, .value = 4.0},
        {.at_ns = 60 * second + 3, .value = 3.0},
        {.at_ns = 0, .value = 99.0},
        {.at_ns = 130 * second, .value = 5.0}};
    CHECK(table.seed(h, observed, 7));

    const auto fine = table.columns(h, market_level::fine);
    REQUIRE(fine.size == 2);
    const auto medium = table.columns(h, market_level::medium);
    REQUIRE(medium.size == 2);
    /* The bucket holds the last observation's time, its close, first open. */
    CHECK(medium.at_ns[0] == 60 * second + 3);
    CHECK(medium.value[0] == 3.0);
    CHECK(medium.open[0] == 2.0);
    CHECK(medium.high[0] == 4.0);
    CHECK(medium.low[0] == 2.0);
    CHECK(medium.at_ns[1] == 130 * second);
    const auto coarse = table.columns(h, market_level::coarse);
    REQUIRE(coarse.size == 1);
    CHECK(coarse.value[0] == 5.0);
    CHECK(coarse.high[0] == 5.0);
    CHECK(coarse.low[0] == 2.0);
    CHECK(table.observed_at_ns(h) == 7);
}

TEST_CASE("a seed stamps only when something changed", "[series][seed]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    const std::vector<market_point> observed{
        {.at_ns = 5 * second, .value = 2.0}};
    CHECK(table.seed(h, observed, 3));
    const auto stamp = table.version(h);
    CHECK(stamp != 0);

    CHECK_FALSE(table.seed(h, observed, 3));
    CHECK(table.version(h) == stamp);

    table.fail(h, "boom", 4);
    const auto failed = table.version(h);
    CHECK(table.seed(h, observed, 5));
    CHECK(table.failure(h).empty());
    CHECK(table.version(h) > failed);
    CHECK(table.observed_at_ns(h) == 5);

    CHECK_FALSE(table.seed(h, {}, 5));
}

TEST_CASE("a catalogue refreshes held rows and keeps their rings",
    "[series][catalogue]") {
    series_table table;
    const auto held = table.intern(instance_id, account_id, "s-1");
    const auto gone = table.intern(instance_id, account_id, "s-2");
    table.append(held, market_level::fine, pt(1, 1.0), 1);
    table.append(gone, market_level::fine, pt(1, 2.0), 1);
    CHECK_FALSE(table.info(held)->listed);
    CHECK(table.catalogue(instance_id, account_id) == nullptr);
    const auto stamp = table.version(held);

    table.apply_catalogue(instance_id, account_id,
        {row("s-1", "uri-1", "party-1"), row("s-2", "uri-2"),
         row("s-3", "uri-3")});
    const auto* first = table.catalogue(instance_id, account_id);
    REQUIRE(first != nullptr);
    const auto generation = first->generation;
    CHECK(generation != 0);
    CHECK(first->series.size() == 3);
    CHECK(first->row_of("s-3")->uri == "uri-3");
    CHECK(first->row_of("s-9") == nullptr);
    CHECK(table.info(held)->uri == "uri-1");
    CHECK(table.info(held)->party_id == "party-1");
    CHECK(table.info(held)->listed);
    CHECK(table.version(held) == stamp);
    CHECK(table.size() == 2);

    table.apply_catalogue(instance_id, account_id, {row("s-1", "uri-1b", "party-1b")});
    CHECK(table.catalogue(instance_id, account_id)->generation > generation);
    CHECK(table.info(held)->uri == "uri-1b");
    CHECK(table.info(held)->party_id == "party-1b");
    CHECK_FALSE(table.info(gone)->listed);
    CHECK(table.contains(gone));
    CHECK(table.version(held) == stamp);
    CHECK(table.version(gone) != 0);
    check_columns(table.columns(gone, market_level::fine), {pt(1, 2.0)}, 0);
    CHECK(table.size() == 2);
}

TEST_CASE("a new row takes its description from the catalogue",
    "[series][catalogue]") {
    series_table table;
    table.apply_catalogue(instance_id, account_id, {row("s-1", "uri-1", "party-1")});
    const auto h = table.intern(instance_id, account_id, "s-1");
    CHECK(table.info(h)->uri == "uri-1");
    CHECK(table.info(h)->listed);
    const auto unlisted = table.intern(instance_id, account_id, "s-2");
    CHECK_FALSE(table.info(unlisted)->listed);
}

TEST_CASE("a catalogue read moves its generation and state", "[series][catalogue]") {
    series_table table;
    table.begin_catalogue(instance_id, account_id);
    const auto* cat = table.catalogue(instance_id, account_id);
    REQUIRE(cat != nullptr);
    CHECK(cat->state == hedgr::model::state::catalogue_state::reading);
    const auto reading = cat->generation;

    table.fail_catalogue(instance_id, account_id, "no responder");
    CHECK(cat->state == hedgr::model::state::catalogue_state::failed);
    CHECK(cat->error == "no responder");
    const auto failed = cat->generation;
    CHECK(failed > reading);

    table.apply_catalogue(instance_id, account_id, {row("s-1")});
    CHECK(cat->state == hedgr::model::state::catalogue_state::ready);
    CHECK(cat->error.empty());
    CHECK(cat->generation > failed);
    CHECK(table.catalogue(instance_id, "a-2") == nullptr);
}

namespace {

/** A row with the given number of fine points, then released. */
hedgr::model::state::series_handle filled(series_table& table,
    const std::string& id, std::int64_t newest_sec) {
    const auto h = table.intern(instance_id, account_id, id);
    table.acquire(h);
    for (std::int64_t s = newest_sec - 999; s <= newest_sec; ++s)
        table.append(h, market_level::fine, pt(s, 1.0), 1);
    return h;
}

}

TEST_CASE("eviction removes unwatched rows, least recently released first",
    "[series][evict]") {
    series_table table;
    /* The watched row is the least recently touched of all. */
    const auto watched = filled(table, "w", 5000);
    /*
     * Interned a, b, c, d, but released b, a, d, c, and holding data that ages
     * a newest to d oldest, so release order, intern order and data age each
     * name a different row.
     */
    const auto a = filled(table, "a", 4000);
    const auto b = filled(table, "b", 3000);
    const auto c = filled(table, "c", 2000);
    const auto d = filled(table, "d", 1000);
    table.release(b);
    table.release(a);
    table.release(d);
    table.release(c);
    REQUIRE(table.size() == 5);
    const auto per_row = table.resident_bytes() / 5;
    REQUIRE(per_row > 0);

    table.set_budget(per_row * 4 + per_row / 2);
    CHECK_FALSE(table.contains(b));
    CHECK(table.contains(a));
    CHECK(table.contains(c));
    CHECK(table.contains(d));
    CHECK(table.contains(watched));

    table.set_budget(per_row * 3 + per_row / 2);
    CHECK_FALSE(table.contains(a));
    CHECK(table.contains(c));
    CHECK(table.contains(d));
    CHECK(table.contains(watched));
    CHECK(table.resident_bytes() <= per_row * 3 + per_row / 2);

    CHECK_FALSE(table.find(instance_id, account_id, "a").has_value());
    CHECK(table.find(instance_id, account_id, "c") == c);
}

TEST_CASE("a release that leaves a watcher behind evicts nothing",
    "[series][evict]") {
    /* A row of 1000 fine points holds 1024 slots of six doubles. */
    constexpr std::size_t per_row = 1024 * 6 * sizeof(double);
    series_table table{per_row * 5 / 2};
    const auto idle = filled(table, "z", 1000);
    table.release(idle);
    const auto shared = filled(table, "x", 1000);
    table.acquire(shared);
    static_cast<void>(filled(table, "w", 1000));
    REQUIRE(table.resident_bytes() == 3 * per_row);
    REQUIRE(table.contains(idle));

    table.release(shared);
    CHECK(table.watchers(shared) == 1);
    CHECK(table.contains(idle));

    table.release(shared);
    CHECK_FALSE(table.contains(idle));
    CHECK(table.contains(shared));
}

TEST_CASE("a watched row is never evicted", "[series][evict]") {
    series_table table;
    const auto watched = filled(table, "w", 100);
    const auto other = filled(table, "o", 200);
    table.set_budget(0);
    CHECK(table.contains(watched));
    CHECK(table.contains(other));
    CHECK(table.resident_bytes() > 0);
    CHECK(table.columns(watched, market_level::fine).size == 1000);

    table.release(other);
    CHECK_FALSE(table.contains(other));
    CHECK(table.contains(watched));
}

TEST_CASE("a release over budget evicts and under budget keeps",
    "[series][evict]") {
    series_table table{1};
    const auto first = filled(table, "a", 1000);
    const auto second_row = filled(table, "b", 1000);
    table.acquire(first);
    table.release(first);
    CHECK(table.contains(first));
    table.release(first);
    CHECK_FALSE(table.contains(first));
    CHECK(table.contains(second_row));

    series_table roomy;
    const auto kept = filled(roomy, "a", 1000);
    roomy.release(kept);
    CHECK(roomy.contains(kept));
    CHECK(roomy.watchers(kept) == 0);
}

TEST_CASE("a row watched twice stays until both watchers leave",
    "[series][evict]") {
    series_table table{0};
    const auto h = filled(table, "a", 1000);
    table.acquire(h);
    CHECK(table.watchers(h) == 2);
    table.release(h);
    CHECK(table.contains(h));
    table.release(h);
    CHECK_FALSE(table.contains(h));
}

TEST_CASE("forgetting a scope removes its rows and catalogue only",
    "[series][scope]") {
    series_table table;
    const auto mine = table.intern(instance_id, account_id, "s-1");
    const auto mine_two = table.intern(instance_id, account_id, "s-2");
    const auto other = table.intern(instance_id, "a-2", "s-1");
    const auto other_inst = table.intern("i-2", account_id, "s-1");
    table.append(other, market_level::fine, pt(1, 1.0), 1);
    table.apply_catalogue(instance_id, account_id, {row("s-1")});
    table.apply_catalogue(instance_id, "a-2", {row("s-1")});
    const auto stamp = table.version(other);

    CHECK(table.forget_scope(instance_id, account_id) == 2);
    CHECK_FALSE(table.contains(mine));
    CHECK_FALSE(table.contains(mine_two));
    CHECK(table.catalogue(instance_id, account_id) == nullptr);
    CHECK_FALSE(table.find(instance_id, account_id, "s-1").has_value());

    CHECK(table.contains(other));
    CHECK(table.contains(other_inst));
    CHECK(table.catalogue(instance_id, "a-2") != nullptr);
    CHECK(table.version(other) == stamp);
    check_columns(table.columns(other, market_level::fine), {pt(1, 1.0)}, 0);
    CHECK(table.size() == 2);
    CHECK(table.forget_scope(instance_id, account_id) == 0);

    const auto again = table.intern(instance_id, account_id, "s-1");
    CHECK(again != mine);
}

TEST_CASE("clearing the table makes every handle stale", "[series][scope]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    table.apply_catalogue(instance_id, account_id, {row("s-1")});
    table.clear();
    CHECK_FALSE(table.contains(h));
    CHECK(table.catalogue(instance_id, account_id) == nullptr);
    CHECK(table.resident_bytes() == 0);
    CHECK(table.size() == 0);
}

TEST_CASE("looking up and reading a held series allocates nothing",
    "[series][cost]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d");
    table.acquire(h);
    const auto capacity = hedgr::model::domain::market_fine_capacity;
    /* Warm up past capacity plus slack so every buffer is full size. */
    for (std::size_t i = 0; i < capacity * 2; ++i)
        table.append(h, market_level::fine, nth(i), 1);

    const auto before = allocations();
    std::size_t found = 0;
    std::uint64_t stamps = 0;
    for (std::size_t i = capacity * 2; i < capacity * 6; ++i) {
        const auto key = table.find(instance_id, account_id,
            "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d");
        found += key.has_value();
        found += table.columns(h, market_level::fine).size;
        stamps += table.version(h);
        found += table.append(h, market_level::fine, nth(i), 1) ==
            append_result::applied;
    }
    CHECK(allocations() - before == 0);
    CHECK(found > 0);
    CHECK(stamps > 0);
    CHECK(table.columns(h, market_level::fine).size == capacity);
}

TEST_CASE("the first watcher and the last are the only recorded changes",
    "[series][changes]") {
    series_table table;
    const auto h = table.intern(instance_id, account_id, "s-1");
    std::vector<hedgr::model::state::series_handle> changes;

    table.drain_changes(changes);
    CHECK(changes.empty());

    table.acquire(h);
    table.acquire(h);
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == h);

    table.release(h);
    table.drain_changes(changes);
    CHECK(changes.empty());

    table.release(h);
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == h);

    table.release(h);
    table.acquire({99, 1});
    table.drain_changes(changes);
    CHECK(changes.empty());
}

TEST_CASE("a catalogue records a held watched row only when its watch state "
    "moved", "[series][changes]") {
    series_table table;
    const auto watched = table.intern(instance_id, account_id, "s-1");
    const auto idle = table.intern(instance_id, account_id, "s-2");
    table.acquire(watched);
    std::vector<hedgr::model::state::series_handle> changes;
    table.drain_changes(changes);

    table.apply_catalogue(instance_id, account_id,
        {row("s-1", "u-1", "p-1"), row("s-2", "u-2", "p-1")});
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == watched);

    table.apply_catalogue(instance_id, account_id,
        {row("s-2", "u-2", "p-1"), row("s-1", "u-1", "p-1")});
    table.drain_changes(changes);
    CHECK(changes.empty());

    table.apply_catalogue(instance_id, account_id,
        {row("s-1", "u-1b", "p-1"), row("s-2", "u-2", "p-1")});
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);

    table.apply_catalogue(instance_id, account_id,
        {row("s-1", "u-1b", "p-2"), row("s-2", "u-2", "p-1")});
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);

    table.apply_catalogue(instance_id, account_id, {row("s-2", "u-2", "p-1")});
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == watched);
    CHECK_FALSE(table.info(watched)->listed);
    CHECK(table.info(idle)->listed);
}

TEST_CASE("erasing a watched row is a change and an unwatched row is not",
    "[series][changes]") {
    series_table table;
    const auto watched = table.intern(instance_id, account_id, "s-1");
    const auto idle = table.intern(instance_id, account_id, "s-2");
    table.acquire(watched);
    std::vector<hedgr::model::state::series_handle> changes;
    table.drain_changes(changes);

    table.erase(idle);
    table.drain_changes(changes);
    CHECK(changes.empty());

    table.forget_scope(instance_id, account_id);
    table.drain_changes(changes);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == watched);
    CHECK_FALSE(table.contains(watched));
}

TEST_CASE("a scope lists its rows, names them and adds up its watchers",
    "[series][changes]") {
    series_table table;
    const auto a = table.intern(instance_id, account_id, "s-1");
    const auto b = table.intern(instance_id, account_id, "s-2");
    const auto other = table.intern(instance_id, "a-2", "s-1");
    table.acquire(a);
    table.acquire(a);
    table.acquire(other);

    const auto held = table.held(instance_id, account_id);
    REQUIRE(held.size() == 2);
    CHECK(held[0] == a);
    CHECK(held[1] == b);
    CHECK(table.held(instance_id, "nobody").empty());

    const auto key = table.key_of(other);
    REQUIRE(key.has_value());
    CHECK(key->instance_id == instance_id);
    CHECK(key->account_id == "a-2");
    CHECK(key->series_id == "s-1");
    CHECK(table.watcher_total() == 3);

    table.erase(a);
    CHECK_FALSE(table.key_of(a).has_value());
    CHECK(table.watcher_total() == 1);
}

TEST_CASE("forgetting an instance removes every account of it and no other",
    "[series][scope]") {
    series_table table;
    const auto one = table.intern("i-1", "a-1", "s-1");
    const auto two = table.intern("i-1", "a-2", "s-1");
    const auto kept = table.intern("i-2", "a-1", "s-1");
    table.apply_catalogue("i-1", "a-3", {row("s-1")});
    table.apply_catalogue("i-2", "a-1", {row("s-1")});

    CHECK(table.forget_instance("i-1") == 2);
    CHECK_FALSE(table.contains(one));
    CHECK_FALSE(table.contains(two));
    CHECK(table.contains(kept));
    CHECK(table.catalogue("i-1", "a-3") == nullptr);
    CHECK(table.catalogue("i-2", "a-1") != nullptr);
    CHECK(table.size() == 1);
}
