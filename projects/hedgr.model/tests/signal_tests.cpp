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
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using hedgr::model::signal::bus;
using hedgr::model::signal::signal;
using hedgr::model::signal::signal_bus;

/*
 * The bus is the whole mechanism, so what is pinned here is what a listener may
 * rely on: a signal comes back once, in the order it was raised, and a raiser
 * on another thread is delivered the same way. The names are pinned too,
 * because a name registered against a sound is an interface and a rename that
 * no test noticed would unhook the sound silently.
 */

TEST_CASE("a drained signal comes back once and leaves the bus empty",
    "[signal]") {
    signal_bus subject;

    REQUIRE(subject.empty());

    signal first;
    first.name = "instance.readiness.degraded";
    first.detail = "prod stopped being ready";
    first.at_ns = 41;
    subject.raise(std::move(first));

    signal second;
    second.name = "shortcut.activated";
    second.detail = "opened the accounts of prod";
    second.at_ns = 42;
    subject.raise(std::move(second));

    REQUIRE_FALSE(subject.empty());

    const std::vector<signal> drained = subject.drain();
    REQUIRE(drained.size() == 2);
    CHECK(drained[0].name == "instance.readiness.degraded");
    CHECK(drained[0].detail == "prod stopped being ready");
    CHECK(drained[0].at_ns == 41);
    CHECK(drained[1].name == "shortcut.activated");
    CHECK(drained[1].at_ns == 42);

    /*
     * The second drain is the one that proves delivery happens once. A bus that
     * kept the batch would play the same sound again on every frame.
     */
    REQUIRE(subject.empty());
    CHECK(subject.drain().empty());
}

TEST_CASE("a signal raised on another thread is delivered to the drain",
    "[signal]") {
    signal_bus subject;
    constexpr int count = 64;

    std::thread raiser([&subject] {
        for (int i = 0; i < count; ++i) {
            signal value;
            value.name = "instance.reachability.lost";
            value.at_ns = i;
            subject.raise(std::move(value));
        }
    });
    raiser.join();

    const std::vector<signal> drained = subject.drain();
    REQUIRE(drained.size() == static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        CHECK(drained[static_cast<std::size_t>(i)].at_ns == i);
}

TEST_CASE("the process bus is one bus and drains on demand", "[signal]") {
    /*
     * The accessor is what a raise site uses, so it has to answer the same
     * instance every time and start empty for the test that uses it. Anything
     * left from another test is drained first rather than assumed away.
     */
    (void)bus().drain();

    signal value;
    value.name = std::string(hedgr::model::signal::names::shortcut_activated);
    bus().raise(std::move(value));

    REQUIRE_FALSE(bus().empty());
    const std::vector<signal> drained = bus().drain();
    REQUIRE(drained.size() == 1);
    CHECK(drained[0].name == "shortcut.activated");
    CHECK(bus().empty());
}
