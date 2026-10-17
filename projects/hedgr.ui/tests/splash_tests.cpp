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
#include "hedgr.ui/render/splash.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using hedgr::ui::render::splash_timing;

namespace {

constexpr std::int64_t start_ns = 1'000'000'000;
constexpr std::int64_t minimum_ns = 350'000'000;
constexpr std::int64_t maximum_ns = 1'500'000'000;

[[nodiscard]] std::int64_t after(std::int64_t elapsed_ns) {
    return start_ns + elapsed_ns;
}

}

/*
 * The splash's whole behaviour is the predicate below, so these tests state the
 * times a launch actually passes through rather than registering a window. The
 * two answers that matter are the early one and the late one: a splash that
 * outlived its maximum would hide the failure notice behind it, and a splash
 * that ignored the store becoming ready would sit over an interface the
 * operator could already use.
 */
TEST_CASE("the splash's bounds are the ones the launch relies on", "[splash]") {
    const splash_timing timing;
    CHECK(timing.minimum_ns == 350'000'000);
    CHECK(timing.maximum_ns == 1'500'000'000);
}

TEST_CASE("the splash is visible from the first frame", "[splash]") {
    splash_timing timing;
    timing.started_ns = start_ns;

    CHECK(timing.visible(after(0), false));
    CHECK(timing.visible(after(0), true));
    CHECK(timing.visible(after(1), false));
}

TEST_CASE("a ready store ends the splash after the floor, not at once",
    "[splash]") {
    /*
     * An already unlocked store must not blink the splash away on the first
     * frame, because the point of it is to be seen while the sound plays. The
     * floor is inclusive at its end and exclusive at its start, which is what
     * the two checks either side of it pin.
     */
    splash_timing timing;
    timing.started_ns = start_ns;

    CHECK(timing.visible(after(minimum_ns - 1), true));
    CHECK_FALSE(timing.visible(after(minimum_ns), true));
    CHECK_FALSE(timing.visible(after(minimum_ns + 1), true));
}

TEST_CASE("a store that never opens still loses the splash at the maximum",
    "[splash]") {
    splash_timing timing;
    timing.started_ns = start_ns;

    CHECK(timing.visible(after(minimum_ns), false));
    CHECK(timing.visible(after(maximum_ns - 1), false));
    CHECK_FALSE(timing.visible(after(maximum_ns), false));
    CHECK_FALSE(timing.visible(after(maximum_ns + 1), false));
}

TEST_CASE("a clock reading before the start keeps the splash up", "[splash]") {
    /*
     * The start is read from the same steady clock as the frame, so a reading
     * before it should not happen. Treating it as "not yet shown" rather than
     * "already over" means a clock that jumped backwards shows the splash for
     * an extra moment instead of skipping it.
     */
    splash_timing timing;
    timing.started_ns = start_ns;

    CHECK(timing.visible(start_ns - 1, false));
    CHECK(timing.visible(start_ns - 1, true));
}
