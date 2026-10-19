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
#include "hedgr.model/state/condition_table.hpp"
#include <catch2/catch_all.hpp>
#include <string>

using hedgr::model::state::condition_key;
using hedgr::model::state::condition_lifetime;
using hedgr::model::state::condition_severity;
using hedgr::model::state::condition_source;
using hedgr::model::state::condition_table;
using hedgr::model::state::lifetime_of;

namespace {

constexpr auto error = condition_severity::error;
constexpr auto warning = condition_severity::warning;

[[nodiscard]] condition_key folder(std::string id) {
    return {condition_source::folder, std::move(id)};
}

}

TEST_CASE("raising the same cause again updates its row", "[conditions]") {
    condition_table table;
    table.raise(folder("f1"), error, "cannot save folder f1", 10);
    table.raise(folder("f1"), error, "cannot save folder f1, again", 20);

    REQUIRE(table.size() == 1);
    REQUIRE(table.rows()[0].message == "cannot save folder f1, again");
    REQUIRE(table.rows()[0].raised_ns == 20);
}

TEST_CASE("two causes raised in one frame are both kept", "[conditions]") {
    /*
     * The failure the one-string surface had: the second write in a frame
     * replaced the first before anyone had read it. Same timestamp on purpose,
     * which is what "one frame" means to the table.
     */
    condition_table table;
    table.raise(folder("f1"), error, "store refused the folder", 10);
    table.raise({condition_source::session, "acct"}, error,
        "the certificate would not open", 10);

    REQUIRE(table.size() == 2);
    REQUIRE(table.rows()[0].message == "store refused the folder");
    REQUIRE(table.rows()[1].message == "the certificate would not open");
}

TEST_CASE("the table is bounded and drops the oldest raise", "[conditions]") {
    condition_table table(3);
    table.raise(folder("f1"), error, "one", 1);
    table.raise(folder("f2"), error, "two", 2);
    table.raise(folder("f3"), error, "three", 3);
    table.raise(folder("f4"), error, "four", 4);

    REQUIRE(table.size() == 3);
    REQUIRE(table.capacity() == 3);
    REQUIRE_FALSE(table.clear(folder("f1")));
    REQUIRE(table.rows()[0].key == folder("f2"));
    REQUIRE(table.rows()[2].key == folder("f4"));
}

TEST_CASE("a re-raise does not cost a slot when the table is full",
    "[conditions]") {
    condition_table table(2);
    table.raise(folder("f1"), error, "one", 1);
    table.raise(folder("f2"), error, "two", 2);
    table.raise(folder("f1"), error, "one again", 3);

    REQUIRE(table.size() == 2);
    REQUIRE(table.rows()[0].key == folder("f2"));
    REQUIRE(table.rows()[1].message == "one again");
}

TEST_CASE("clearing by key removes that cause and nothing else",
    "[conditions]") {
    condition_table table;
    table.raise(folder("f1"), error, "one", 1);
    table.raise(folder("f2"), error, "two", 2);

    REQUIRE(table.clear(folder("f1")));
    REQUIRE(table.size() == 1);
    REQUIRE(table.rows()[0].key == folder("f2"));
    REQUIRE_FALSE(table.clear(folder("f1")));
}

TEST_CASE("clearing by source removes every cause of that kind",
    "[conditions]") {
    condition_table table;
    table.raise({condition_source::store_lock, {}}, error, "lock", 1);
    table.raise({condition_source::store_lock, "acct"}, error, "locked", 2);
    table.raise(folder("f1"), error, "folder", 3);

    REQUIRE(table.clear(condition_source::store_lock) == 2);
    REQUIRE(table.size() == 1);
    REQUIRE(table.rows()[0].key == folder("f1"));
}

TEST_CASE("a new command clears verdicts and leaves standing conditions",
    "[conditions]") {
    STATIC_REQUIRE(lifetime_of(condition_source::startup)
        == condition_lifetime::standing);
    STATIC_REQUIRE(lifetime_of(condition_source::window_wall)
        == condition_lifetime::standing);
    STATIC_REQUIRE(lifetime_of(condition_source::folder)
        == condition_lifetime::verdict);

    condition_table table;
    table.raise({condition_source::window_wall, {}}, error, "wall", 1);
    table.raise(folder("f1"), error, "folder", 2);
    table.raise({condition_source::session, "acct"}, error, "session", 3);

    REQUIRE(table.clear_verdicts() == 2);
    REQUIRE(table.size() == 1);
    REQUIRE(table.message() == "wall");
}

TEST_CASE("the shown condition is the gravest, then the newest",
    "[conditions]") {
    condition_table table;
    REQUIRE(table.current() == nullptr);
    REQUIRE(table.message().empty());

    table.raise(folder("f1"), error, "first error", 1);
    table.raise(folder("f2"), error, "second error", 2);
    REQUIRE(table.message() == "second error");

    /* A later warning does not displace an error. */
    table.raise(folder("f3"), warning, "a warning", 3);
    REQUIRE(table.message() == "second error");

    /* Raising an older cause again makes it the newest. */
    table.raise(folder("f1"), error, "first error, again", 4);
    REQUIRE(table.message() == "first error, again");

    /* Clearing the shown one falls back to the next by the same rule. */
    REQUIRE(table.clear(folder("f1")));
    REQUIRE(table.message() == "second error");
    REQUIRE(table.clear(folder("f2")));
    REQUIRE(table.message() == "a warning");
}
