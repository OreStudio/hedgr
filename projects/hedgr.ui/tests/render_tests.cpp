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
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

/*
 * There is deliberately no rendering test here. Every renderer in this
 * component calls into ImGui, and ImGui::NewFrame without a live context, a
 * font atlas and a rendering backend aborts rather than returning an error.
 * Standing one up would need a GL context, which this test binary has no way
 * to create and no reason to: the renderers are thin bindings over the model's
 * tables, and the parts of them that carry any logic -- which renderer answers
 * for a kind, and what colour and icon a status maps to -- are exactly the
 * parts tested below, none of which touch ImGui.
 */

using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::ui::render::renderer_for;
using hedgr::ui::render::window_ui_state;

TEST_CASE("every window kind has a renderer", "[renderer]") {
    for (std::size_t value = 0; value < hedgr::model::domain::window_kind_count;
         ++value) {
        const auto kind = static_cast<window_kind>(value);
        INFO("window kind " << value);
        REQUIRE(renderer_for(kind) != nullptr);
    }
}

TEST_CASE("statuses that a person must tell apart have distinct colours",
    "[theme]") {
    const auto authenticated = hedgr::ui::theme::status_colour(
        session_status::authenticated);
    const auto failed =
        hedgr::ui::theme::status_colour(session_status::failed);
    const auto disconnected =
        hedgr::ui::theme::status_colour(session_status::disconnected);

    REQUIRE(authenticated != failed);
    REQUIRE(failed != disconnected);
    REQUIRE(authenticated != disconnected);
}

TEST_CASE("every session status has an icon", "[theme]") {
    for (std::size_t value = 0;
         value < hedgr::model::domain::session_status_count; ++value) {
        const auto icon =
            hedgr::ui::theme::status_icon(static_cast<session_status>(value));
        INFO("status " << value);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("every window kind has an icon", "[theme]") {
    for (std::size_t value = 0; value < hedgr::model::domain::window_kind_count;
         ++value) {
        const auto icon =
            hedgr::ui::theme::window_kind_icon(static_cast<window_kind>(value));
        INFO("window kind " << value);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("a window's scratch state starts empty and masked", "[state]") {
    const window_ui_state state;

    REQUIRE(state.filter[0] == '\0');
    REQUIRE(state.draft_password[0] == '\0');
    REQUIRE_FALSE(state.password_visible);
    REQUIRE(state.selected_row == -1);
    REQUIRE(state.pending_remove_id.empty());
    REQUIRE(state.draft_principal.empty());
    REQUIRE_FALSE(state.show_detail);
}

/*
 * The lock gate, tested as a predicate rather than as pixels.
 *
 * Every renderer in this component calls into ImGui, and ImGui::NewFrame
 * without a live context, a font atlas and a rendering backend aborts rather
 * than returning an error. So the rule that decides whether the Instances
 * window is drawn at all lives in a function that touches no ImGui, and it is
 * called directly here. A predicate a test can call is worth more than a
 * comment saying the code checks something.
 */
namespace {

using hedgr::model::command::command_queue;
using hedgr::model::domain::store_lock;
using hedgr::model::state::account_table;
using hedgr::model::state::folder_table;
using hedgr::model::state::instance_table;
using hedgr::model::state::label_table;
using hedgr::model::state::labelling_table;
using hedgr::model::state::probe_table;
using hedgr::model::state::session_table;
using hedgr::model::state::window_table;
using hedgr::ui::render::render_input;
using hedgr::ui::render::shows_instances;

/** @brief Every table a render_input borrows, so it can safely borrow them. */
struct empty_state final {
    instance_table instances;
    account_table accounts;
    folder_table folders;
    label_table labels;
    labelling_table labellings;
    session_table sessions;
    probe_table probes;
    window_table windows;
    command_queue commands;
};

/** @brief A render_input over empty tables, carrying @p lock. */
render_input input_with(empty_state& state, store_lock lock) {
    return render_input{
        .instances = state.instances,
        .accounts = state.accounts,
        .folders = state.folders,
        .labels = state.labels,
        .labellings = state.labellings,
        .sessions = state.sessions,
        .probes = state.probes,
        .windows = state.windows,
        .commands = state.commands,
        .store_lock = lock,
    };
}

}

TEST_CASE("the instances window is drawn only while the store is unlocked",
    "[lock]") {
    empty_state state;

    REQUIRE(shows_instances(input_with(state, store_lock::unlocked)));

    /*
     * Absent is a first run and locked is a store at rest; neither is a
     * failure, and neither draws the catalogue. Failed draws it no more than
     * the other two do, because it is the same closed lock after a refusal.
     */
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::absent)));
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::locked)));
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::failed)));
}

TEST_CASE("the gate closes again when the store is locked once more", "[lock]") {
    empty_state state;

    /*
     * The same input object, moved through the states a run actually visits:
     * absent on a first launch, unlocked after the password was created, locked
     * again when the operator locked it. The gate has to follow, and it has to
     * open on the way back too, or a lock would be a one-way door.
     */
    auto input = input_with(state, store_lock::absent);
    REQUIRE_FALSE(shows_instances(input));

    input.store_lock = store_lock::unlocked;
    REQUIRE(shows_instances(input));

    input.store_lock = store_lock::locked;
    REQUIRE_FALSE(shows_instances(input));

    input.store_lock = store_lock::unlocked;
    REQUIRE(shows_instances(input));
}

TEST_CASE("every lock state has an icon", "[lock]") {
    for (std::size_t value = 0;
         value < hedgr::model::domain::store_lock_count; ++value) {
        const auto lock = static_cast<store_lock>(value);
        INFO("lock state " << value);
        const auto icon = hedgr::ui::theme::store_lock_icon(lock);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("the lock states a person must tell apart have distinct icons",
    "[lock]") {
    /*
     * Unlocked, locked and absent have to be three different glyphs. Two of
     * them as a padlock would be a state an operator cannot read at a glance,
     * and absent is the one that wants an action rather than an unlock.
     */
    const auto unlocked = std::string{hedgr::ui::theme::store_lock_icon(
        store_lock::unlocked)};
    const auto locked =
        std::string{hedgr::ui::theme::store_lock_icon(store_lock::locked)};
    const auto absent =
        std::string{hedgr::ui::theme::store_lock_icon(store_lock::absent)};

    REQUIRE(unlocked != locked);
    REQUIRE(absent != locked);
    REQUIRE(absent != unlocked);
}

TEST_CASE("a failed lock is drawn as a failure and a working one is not",
    "[lock]") {
    /*
     * Only @c failed says something went wrong, so only @c failed may use the
     * colour this interface uses for failure. If locked or absent shared it,
     * the ordinary condition of a store at rest would read as a fault.
     */
    const auto failed =
        hedgr::ui::theme::store_lock_colour(store_lock::failed);
    const auto unlocked =
        hedgr::ui::theme::store_lock_colour(store_lock::unlocked);
    const auto locked = hedgr::ui::theme::store_lock_colour(store_lock::locked);

    REQUIRE(failed != unlocked);
    REQUIRE(failed != locked);
}
