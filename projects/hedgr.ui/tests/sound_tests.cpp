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
#include "hedgr.ui/sound/pack.hpp"
#include "hedgr.ui/sound/player.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using hedgr::model::signal::signal;
using hedgr::ui::sound::player;
using hedgr::ui::sound::registrations;
using hedgr::ui::sound::sound_for;

namespace names = hedgr::model::signal::names;

namespace {

/**
 * @brief A sink that records what it was asked to play and when.
 *
 * The player's own thread writes to this, so every field is behind the mutex
 * and the wait is how a test learns that a name arrived without polling or
 * sleeping for a guessed interval.
 */
struct recording_sink final {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> files;
    std::vector<std::thread::id> threads;
    std::chrono::milliseconds delay{0};

    void push(std::string_view file) {
        if (delay.count() > 0)
            std::this_thread::sleep_for(delay);
        const auto caller = std::this_thread::get_id();
        {
            std::lock_guard lock(mutex);
            files.emplace_back(file);
            threads.push_back(caller);
        }
        cv.notify_all();
    }

    [[nodiscard]] bool wait_for(std::size_t count,
        std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout,
            [this, count] { return files.size() >= count; });
    }
};

[[nodiscard]] signal named(std::string_view name, std::string detail) {
    signal value;
    value.name = std::string(name);
    value.detail = std::move(detail);
    value.at_ns = 1;
    return value;
}

}

/*
 * The name is the whole interface, so the mapping from name to file is the
 * thing a rename can break silently. These tests state each name and its file
 * explicitly rather than only checking that some file is returned, because a
 * test that accepted any answer would pass on a table that had been reordered
 * under it.
 */
TEST_CASE("every registered name maps to its own short sound", "[sound]") {
    REQUIRE(registrations().size() == 10);

    const auto started = sound_for(names::application_started);
    REQUIRE(started.has_value());
    CHECK(*started == "bong_001.wav");

    const auto intro = sound_for(names::application_intro);
    REQUIRE(intro.has_value());
    CHECK(*intro == "intro_swell.wav");

    const auto hovered = sound_for(names::shortcut_hovered);
    REQUIRE(hovered.has_value());
    CHECK(*hovered == "click1.wav");

    const auto closed = sound_for(names::window_closed);
    REQUIRE(closed.has_value());
    CHECK(*closed == "bong_001.wav");

    const auto degraded = sound_for(names::instance_readiness_degraded);
    REQUIRE(degraded.has_value());
    CHECK(*degraded == "error_001.wav");

    const auto recovered = sound_for(names::instance_readiness_recovered);
    REQUIRE(recovered.has_value());
    CHECK(*recovered == "confirmation_001.wav");

    const auto bootstrapping = sound_for(names::instance_readiness_bootstrapping);
    REQUIRE(bootstrapping.has_value());
    CHECK(*bootstrapping == "bong_001.wav");

    const auto lost = sound_for(names::instance_reachability_lost);
    REQUIRE(lost.has_value());
    CHECK(*lost == "error_002.wav");

    const auto restored = sound_for(names::instance_reachability_restored);
    REQUIRE(restored.has_value());
    CHECK(*restored == "confirmation_002.wav");

    const auto clicked = sound_for(names::shortcut_activated);
    REQUIRE(clicked.has_value());
    CHECK(*clicked == "click1.wav");
}

TEST_CASE("the startup signal raised on the bus reaches the player as a file",
    "[sound][player]") {
    /*
     * The launch raises a name rather than calling the player, so the proof
     * that the startup sound happens is the whole path: the name goes on the
     * bus, the drain the frame loop performs hands it to the player, and the
     * mapping turns it into a file. The sink is the player's own, so what
     * arrives here is what the audio thread would decode. Nothing about this
     * asserts that a speaker was audible.
     */
    auto recorder = std::make_shared<recording_sink>();
    player subject([recorder](std::string_view file) { recorder->push(file); });
    subject.start();
    REQUIRE(subject.started());

    (void)hedgr::model::signal::bus().drain();

    signal started;
    started.name = std::string(names::application_started);
    started.detail = "Hedgr started";
    started.at_ns = 1;
    hedgr::model::signal::bus().raise(std::move(started));

    const auto drained = hedgr::model::signal::bus().drain();
    REQUIRE(drained.size() == 1);
    CHECK(subject.submit(drained) == 1);

    REQUIRE(recorder->wait_for(1, std::chrono::seconds(5)));
    CHECK(recorder->files.at(0) == "bong_001.wav");
    CHECK(recorder->threads.at(0) != std::this_thread::get_id());

    subject.stop();
}

TEST_CASE("the intro signal raised on the bus reaches the player as the swell",
    "[sound][player]") {
    auto recorder = std::make_shared<recording_sink>();
    player subject([recorder](std::string_view file) { recorder->push(file); });
    subject.start();
    REQUIRE(subject.started());

    (void)hedgr::model::signal::bus().drain();

    signal intro;
    intro.name = std::string(names::application_intro);
    intro.detail = "Hedgr intro";
    intro.at_ns = 1;
    hedgr::model::signal::bus().raise(std::move(intro));

    const auto drained = hedgr::model::signal::bus().drain();
    REQUIRE(drained.size() == 1);
    CHECK(subject.submit(drained) == 1);

    REQUIRE(recorder->wait_for(1, std::chrono::seconds(5)));
    CHECK(recorder->files.at(0) == "intro_swell.wav");

    subject.stop();
}

TEST_CASE("a name nobody registered maps to nothing", "[sound]") {    CHECK_FALSE(sound_for("instance.readiness.unknown").has_value());
    CHECK_FALSE(sound_for("shortcut").has_value());
    CHECK_FALSE(sound_for("").has_value());

    /*
     * A registration answers a family as well as a name, because the name is
     * matched as a prefix. A more specific name still takes the longer entry
     * when one is registered beside a broader one, which is what lets a later
     * override be added without disturbing the family it sits in.
     */
    const auto family = sound_for("instance.readiness.degraded.again");
    REQUIRE(family.has_value());
    CHECK(*family == "error_001.wav");
}

TEST_CASE("a signal reaches the player and plays off the caller's thread",
    "[sound][player]") {
    auto recorder = std::make_shared<recording_sink>();
    player subject([recorder](std::string_view file) { recorder->push(file); });
    subject.start();
    REQUIRE(subject.started());

    const std::vector<signal> signals{
        named(names::instance_readiness_degraded, "prod stopped being ready"),
        named("shortcut.activated", "open the accounts of prod"),
    };

    const auto caller = std::this_thread::get_id();
    const std::size_t queued = subject.submit(signals);
    CHECK(queued == 2);

    REQUIRE(recorder->wait_for(2, std::chrono::seconds(5)));
    CHECK(recorder->files.at(0) == "error_001.wav");
    CHECK(recorder->files.at(1) == "click1.wav");
    CHECK(recorder->threads.at(0) != caller);
    CHECK(recorder->threads.at(1) != caller);

    subject.stop();
    CHECK_FALSE(subject.started());
}

TEST_CASE("an unregistered signal is skipped rather than queued", "[sound][player]") {
    auto recorder = std::make_shared<recording_sink>();
    player subject([recorder](std::string_view file) { recorder->push(file); });
    subject.start();

    const std::vector<signal> signals{
        named("nobody.listens", "a signal with no sound"),
        named(names::shortcut_activated, "a click"),
    };
    CHECK(subject.submit(signals) == 1);

    REQUIRE(recorder->wait_for(1, std::chrono::seconds(5)));
    CHECK(recorder->files.size() == 1);
    CHECK(recorder->files.at(0) == "click1.wav");

    subject.stop();
}

TEST_CASE("submitting returns without waiting for the sink", "[sound][player]") {
    /*
     * The frame loop calls submit and must not wait for a sound to be decoded.
     * A sink that holds the worker for a third of a second is the whole of the
     * demonstration: a caller that blocked on it would measure that third, and
     * the bound below is loose enough not to depend on machine speed.
     */
    auto recorder = std::make_shared<recording_sink>();
    recorder->delay = std::chrono::milliseconds(300);
    player subject([recorder](std::string_view file) { recorder->push(file); });
    subject.start();

    const std::vector<signal> signals{
        named(names::instance_reachability_lost, "prod is unreachable"),
    };

    const auto begin = std::chrono::steady_clock::now();
    (void)subject.submit(signals);
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    CHECK(elapsed < std::chrono::milliseconds(100));

    subject.stop();
}

TEST_CASE("a player that never started drops every signal", "[sound][player]") {
    /*
     * A host with no output device leaves the player unstarted, which is the
     * same state as a player that has not been started yet or has been stopped.
     * All three take the branch pinned here: nothing is queued, nothing is
     * played, and the caller is not told the name was accepted. This is how the
     * no-device path is checked on a host that does have a device.
     */
    auto recorder = std::make_shared<recording_sink>();
    player subject([recorder](std::string_view file) { recorder->push(file); });

    const std::vector<signal> signals{
        named(names::shortcut_activated, "a click"),
    };
    CHECK(subject.submit(signals) == 0);
    CHECK(recorder->files.empty());

    subject.start();
    subject.stop();
    CHECK(subject.submit(signals) == 0);
    CHECK(recorder->files.empty());
}

TEST_CASE("a player with no output device stays silent and safe", "[sound][player]") {
    /*
     * A host may or may not have an output device, so the default player cannot
     * be asserted either way. What is asserted is that the absence of one is a
     * state rather than a failure: start returns, submit returns, and the
     * destructor joins cleanly. The device result is reported so a run on a
     * host that does have a sound card is visible in the test output.
     */
    player subject;
    subject.start();
    INFO("default player started: " << subject.started());

    const std::vector<signal> signals{
        named(names::instance_readiness_degraded, "prod stopped being ready"),
        named(names::shortcut_activated, "a click"),
    };
    CHECK_NOTHROW((void)subject.submit(signals));
    subject.stop();
}
