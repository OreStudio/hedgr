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
using hedgr::ui::widget::status_severity;

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
     * A notice may not take the bar from a condition that is still true. The
     * condition is what the operator has to act on and the notice is a remark
     * about something that just happened; showing the remark and losing the
     * problem for a few seconds is how a failure goes unread.
     *
     * The raise on the next frame is what a caller does: the condition is
     * re-announced, and it changes nothing either way.
     */
    status_bar bar;
    bar.show_condition("unreachable", at_start);
    bar.show_event("a row was removed", at_start);
    REQUIRE(bar.message() == "unreachable");

    bar.show_condition("unreachable", at_start);
    REQUIRE(bar.message() == "unreachable");
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

TEST_CASE("an error is held until another error replaces it", "[status]") {
    /*
     * An error is not an event. A notice describes something that happened and
     * is worth missing if the operator looked away; a failure describes the
     * state of the store, and a refused write that vanished after six seconds
     * would read as a write that succeeded. So no amount of time takes it
     * away, and the literal below is what the runtime records for a store that
     * would not come up.
     */
    status_bar bar;
    const std::string refused =
        "cannot bring the store up to date: the store at /home/o/.cache/hedgr/"
        "hedgr.db was written with schema version 6 but this build expects "
        "version 7; this store is pre-release and is not migrated, so delete "
        "the file and start again (schema mismatch)";

    bar.show_error(refused, at_start);
    REQUIRE(bar.message() == refused);
    REQUIRE(bar.severity() == status_severity::error);

    /* Long past any event's life, and still on the bar. */
    bar.advance(at_start + status_event_duration_ns * 100);
    REQUIRE(bar.message() == refused);
    REQUIRE(bar.alpha(at_start + status_event_duration_ns * 100) == 1.0f);

    /* Raised again on the next frame, which is what the frame does. */
    bar.show_error(refused, at_start);
    REQUIRE(bar.message() == refused);

    /* And the next failure replaces it, because it is the newer fact. */
    bar.show_error("cannot remove the root folder", at_start);
    REQUIRE(bar.message() == "cannot remove the root folder");
}

TEST_CASE("a remark does not displace a failure", "[status]") {
    /*
     * The reason the bar has a ladder rather than a first-come rule. The store
     * opening from the environment is a remark about how the store was opened;
     * a save the store refused is a problem the operator has to act on. Telling
     * them apart at a glance is the requirement, and losing the problem to the
     * remark one frame later is the defect.
     */
    status_bar bar;
    bar.show_error("cannot save instance inst-1 (constraint failed)", at_start);
    bar.show_event("the store was opened from the environment", at_start);
    bar.show_condition("ores.dev.brave_hopper is unreachable", at_start);

    REQUIRE(bar.message() == "cannot save instance inst-1 (constraint failed)");
    REQUIRE(bar.severity() == status_severity::error);

    /*
     * A problem outranks a remark the other way round: a remark may not take
     * the bar from a condition that is still true either.
     */
    status_bar other;
    other.show_condition("ores.dev.brave_hopper is unreachable", at_start);
    other.show_event("a row was removed", at_start);
    REQUIRE(other.message() == "ores.dev.brave_hopper is unreachable");
    REQUIRE(other.severity() == status_severity::problem);

    /* A notice still takes a bar that is only carrying a notice. */
    status_bar quiet;
    quiet.show_event("the store was opened", at_start);
    quiet.show_event("a row was removed", at_start);
    REQUIRE(quiet.message() == "a row was removed");
    REQUIRE(quiet.severity() == status_severity::notice);
}

TEST_CASE("withdrawing an error leaves everything else alone", "[status]") {
    /*
     * The dismissal is the operator saying they have read the failure, and it
     * is not a general clear: a remark that is still within its life belongs to
     * somebody who has not asked for it to go.
     */
    status_bar bar;
    bar.show_error("cannot save the seeded instance inst-1", at_start);
    bar.clear_error();
    REQUIRE(bar.empty());

    bar.show_condition("unreachable", at_start);
    bar.clear_error();
    REQUIRE(bar.message() == "unreachable");

    /*
     * And an event on the bar is not an error's to take away either. The event
     * was raised while the problem stood, so it never reached the bar; it is
     * the problem that is on it, and it stays.
     */
    bar.show_event("a row was removed", at_start);
    bar.clear_error();
    REQUIRE(bar.message() == "unreachable");

    /*
     * And the failure is gone rather than merely hidden: the caller's next
     * raise is the same text, and it comes back because the dismissal cleared
     * it, which is what makes a dismissal mean anything.
     */
    bar.show_error("cannot save the seeded instance inst-1", at_start);
    REQUIRE(bar.message() == "cannot save the seeded instance inst-1");
}
