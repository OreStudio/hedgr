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
#include "hedgr.model/command/channel.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <vector>

using hedgr::model::command::channel;

namespace {

/*
 * A wake is a plain function pointer, so what it counts has to live outside
 * it. Each test resets it before use.
 */
std::atomic<int> wakes{0};

void count_wake() { wakes.fetch_add(1); }

}

TEST_CASE("a channel without a wake buffers and drains as before",
    "[channel]") {
    channel<int> subject;
    subject.push(1);
    subject.push(2);

    REQUIRE(subject.drain() == std::vector<int>{1, 2});
    REQUIRE(subject.size() == 0);
}

TEST_CASE("a push into an empty channel wakes the reader once per drain",
    "[channel]") {
    wakes = 0;
    channel<int> subject;
    subject.set_wake(&count_wake);

    subject.push(1);
    REQUIRE(wakes == 1);

    subject.push(2);
    subject.push(3);
    REQUIRE(wakes == 1);

    REQUIRE(subject.drain() == std::vector<int>{1, 2, 3});
    REQUIRE(wakes == 1);

    subject.push(4);
    REQUIRE(wakes == 2);
}

TEST_CASE("a cleared wake is never called again", "[channel]") {
    wakes = 0;
    channel<int> subject;
    subject.set_wake(&count_wake);
    subject.set_wake(nullptr);

    subject.push(1);
    (void)subject.drain();
    subject.push(2);

    REQUIRE(wakes == 0);
}
