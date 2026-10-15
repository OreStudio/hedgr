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
#include "hedgr.ui/widget/status_bar.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

using hedgr::ui::widget::status_bar;
using hedgr::ui::widget::status_event_duration_ns;

namespace {

/** @brief A monotonic instant, in nanoseconds, for a test to age the bar by. */
constexpr std::int64_t at_start = 1'000'000'000;
constexpr std::int64_t one_second = 1'000'000'000;

}

TEST_CASE("a silent bar says nothing", "[status]") {
    const status_bar bar;

    REQUIRE(bar.empty());
    REQUIRE(bar.message().empty());
    REQUIRE(bar.alpha(at_start) == 1.0f);
}

TEST_CASE("an event notice is shown, then goes on its own", "[status]") {
    /*
     * The whole point of the bar's first kind: a notice that stayed would
     * become furniture, and the operator said so when they asked for this. The
     * boundary is pinned on both sides, because a notice that never left or
     * one that left on the frame it arrived would both look like a defect.
     */
    status_bar bar;
    bar.show_event("the store was opened", at_start);

    REQUIRE_FALSE(bar.empty());
    REQUIRE(bar.message() == "the store was opened");

    bar.advance(at_start + one_second);
    REQUIRE_FALSE(bar.empty());
    REQUIRE(bar.alpha(at_start + one_second) == 1.0f);

    bar.advance(at_start + status_event_duration_ns - 2 * one_second);
    REQUIRE_FALSE(bar.empty());

    /* The last half second is the fade rather than a jump to nothing. */
    const auto half_way_out = at_start + status_event_duration_ns - 250'000'000;
    bar.advance(half_way_out);
    REQUIRE_FALSE(bar.empty());
    REQUIRE(bar.alpha(half_way_out) > 0.0f);
    REQUIRE(bar.alpha(half_way_out) < 1.0f);

    bar.advance(at_start + status_event_duration_ns);
    REQUIRE(bar.empty());
    REQUIRE(bar.message().empty());
}

TEST_CASE("a condition is held until it is withdrawn", "[status]") {
    /*
     * A condition is not history, so no amount of time takes it away. This is
     * the property the unreachable-host message will need: it describes what
     * is true now, and a bar that faded it would be reporting an outage as
     * something that had stopped.
     */
    status_bar bar;
    bar.show_condition("ores.dev.brave_hopper is unreachable", at_start);

    REQUIRE(bar.message() == "ores.dev.brave_hopper is unreachable");

    bar.advance(at_start + status_event_duration_ns * 100);
    REQUIRE_FALSE(bar.empty());
    REQUIRE(bar.alpha(at_start + status_event_duration_ns * 100) == 1.0f);

    bar.clear_condition();
    REQUIRE(bar.empty());
}

TEST_CASE("raising a condition every frame does not re-announce it", "[status]") {
    /*
     * Whatever owns a condition raises it on every frame it is still true. The
     * raise must be idempotent, or a condition would be re-announced sixty
     * times a second for as long as it held.
     */
    status_bar bar;
    bar.show_condition("unreachable", at_start);
    const auto first = bar.message();

    bar.show_condition("unreachable", at_start);
    REQUIRE(bar.message() == first);
    REQUIRE(bar.message() == "unreachable");
}

TEST_CASE("a condition that changes its message replaces the bar", "[status]") {
    /*
     * The other half of the rule above: an idempotent raise is not a mute one.
     * A condition whose own message has changed -- a different deployment
     * unreachable, a different reason -- is a different fact, and a bar still
     * showing the old one would be reporting a state that no longer holds.
     */
    status_bar bar;
    bar.show_condition("brave_hopper is unreachable", at_start);
    bar.show_condition("festive_dijkstra is unreachable", at_start);

    REQUIRE(bar.message() == "festive_dijkstra is unreachable");
}

TEST_CASE("an event notice is not wiped by a condition already on the bar",
    "[status]") {
    /*
     * The reason the raise is idempotent at all. The event is newer than the
     * condition and is the message the operator has not read; a condition
     * raised again on the next frame must not take the bar back from it.
     */
    status_bar bar;
    bar.show_condition("unreachable", at_start);
    bar.show_event("a row was removed", at_start);

    /* The next frame's raise, saying exactly what it said before. */
    bar.show_condition("unreachable", at_start);
    REQUIRE(bar.message() == "a row was removed");
}

TEST_CASE("withdrawing a condition leaves an event notice alone", "[status]") {
    /*
     * The condition ending is not the event that just happened, and an event
     * still within its life has not been read yet. Clearing it because the
     * bar's other kind went away would be the bar losing a fact it still owes
     * the reader.
     */
    status_bar bar;
    bar.show_event("the store was opened", at_start);
    bar.clear_condition();

    REQUIRE(bar.message() == "the store was opened");
}

TEST_CASE("an empty message is not shown", "[status]") {
    /*
     * A caller with nothing to say must not leave the bar announcing nothing,
     * which would reserve the window's foot for an invisible line.
     */
    status_bar bar;
    bar.show_condition("unreachable", at_start);
    bar.show_event("", at_start);
    REQUIRE(bar.empty());

    bar.show_condition("unreachable", at_start);
    bar.show_condition("", at_start);
    REQUIRE(bar.empty());
}
