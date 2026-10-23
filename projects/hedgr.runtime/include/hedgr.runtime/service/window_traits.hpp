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
#ifndef HEDGR_RUNTIME_SERVICE_WINDOW_TRAITS_HPP
#define HEDGR_RUNTIME_SERVICE_WINDOW_TRAITS_HPP

#include "hedgr.model/domain/window.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace hedgr::runtime::service {

/** @brief What a window's refresh asks the runtime to read. */
enum class read_kind : std::uint8_t {
    /** @brief Nothing real yet: an event carrying the window id comes back. */
    placeholder,
    deployment,
    market_catalogue,
    market_points,
    term_structure,
    crm_batch,
};

/** @brief Where a window's refresh deadline goes at one of the four moments. */
enum class deadline_policy : std::uint8_t {
    /** @brief Zero: the next frame that looks at the column refreshes. */
    due_now,
    /** @brief The end of time: the frame loop never refreshes on its own. */
    parked,
    /** @brief Now plus the window's cadence. */
    after_cadence,
};

/**
 * @brief What the runtime does for a window of one kind.
 *
 * One row per kind, in the enum's order, so the compiler checks that every
 * kind has one. The four deadline columns are the four moments the runtime
 * sets a deadline, and they do not agree with each other on purpose: they
 * pin what the code did when they were spread over four sites, so the move
 * into this table changed nothing. Making them agree is a change of
 * behaviour for a later step, and the table is where it will be made.
 */
struct window_traits final {
    model::domain::window_kind kind;

    /** @brief The cadence a new window of this kind is saved with. */
    std::chrono::milliseconds cadence;

    /** @brief A new window. */
    deadline_policy on_open;

    /** @brief An existing window of a keyed kind, reopened while its pair has a session. */
    deadline_policy on_focus;

    /** @brief A refresh command. */
    deadline_policy on_refresh;

    /** @brief A restored window bound by the sign-in it was saved against. */
    deadline_policy on_rebind;

    /** @brief What a refresh asks for. */
    read_kind refresh_read;

    /** @brief Whether an open or a reopen asks for that read at once. */
    bool reads_on_open;
};

namespace detail {

/**
 * @brief Rejects a table in which some kind's row is missing or out of order.
 *
 * A missing initialiser in the array below would otherwise become a row of
 * zeroes that reads as a chart. Checking that row i names kind i turns it
 * into a build error, the way the renderer table rejects a nil entry.
 */
consteval bool every_kind_has_its_row(
    const std::array<window_traits, model::domain::window_kind_count>& table) {
    for (std::size_t index = 0; index < table.size(); ++index) {
        if (static_cast<std::size_t>(table[index].kind) != index)
            return false;
    }
    return true;
}

/*
 * Why the rows read as they do:
 *
 * - A chart is fed by the session's subscription, so its first backfill is
 *   asked for by the watch rather than by the refresh deadline, and a refresh
 *   is the operator asking for the backfill again. It must not rearm a poll.
 *   The two second cadence is still saved with the window, as it always was,
 *   although nothing polls on it; docs/plans/market-eventing.md says it is
 *   gone, and that is the plan's error.
 * - A command centre reads a deployment, not a frame: six list calls on a
 *   second connection. The one second default would put six requests a
 *   second on the broker for a screen whose numbers move over hours.
 * - A term structure reads its curve the moment it opens or is reopened, so
 *   those two moments park the deadline, while a refresh rearms it and a
 *   rebind brings it due now.
 * - A CRM matrix's cadence belongs to its batch, which pump_crm_reads keeps,
 *   so an open and a refresh park the window's own deadline.
 */
constexpr std::array<window_traits, model::domain::window_kind_count>
make_window_trait_table() {
    using namespace std::chrono_literals;
    using enum model::domain::window_kind;
    using enum deadline_policy;
    return {{
        /* kind              cadence  on_open   on_focus  on_refresh      on_rebind  refresh_read                reads_on_open */
        {chart,            2s,      parked,   parked,   parked,         parked,    read_kind::market_points,    false},
        {grid,             1s,      due_now,  due_now,  after_cadence,  due_now,   read_kind::placeholder,      false},
        {notice,           1s,      due_now,  due_now,  after_cadence,  due_now,   read_kind::placeholder,      false},
        {instance_manager, 1s,      due_now,  due_now,  after_cadence,  due_now,   read_kind::placeholder,      false},
        {accounts,         1s,      due_now,  due_now,  after_cadence,  due_now,   read_kind::placeholder,      false},
        {workspace,        1s,      due_now,  due_now,  after_cadence,  due_now,   read_kind::placeholder,      false},
        {command_centre,   30s,     due_now,  due_now,  after_cadence,  due_now,   read_kind::deployment,       false},
        {series_browser,   60s,     due_now,  due_now,  after_cadence,  due_now,   read_kind::market_catalogue, false},
        {term_structure,   30s,     parked,   parked,   after_cadence,  due_now,   read_kind::term_structure,   true},
        {crm_matrix,       1s,      parked,   due_now,  parked,         due_now,   read_kind::crm_batch,        false},
        {mini_plot,        2s,      parked,   parked,   parked,         parked,    read_kind::market_points,    false},
    }};
}

inline constexpr auto window_trait_table = make_window_trait_table();
static_assert(every_kind_has_its_row(window_trait_table),
    "every window kind must have its own row, in enum order, in the table above");

}

/** @brief The traits of one kind. */
[[nodiscard]] constexpr const window_traits&
traits_of(model::domain::window_kind kind) noexcept {
    return detail::window_trait_table[static_cast<std::size_t>(kind)];
}

}

#endif
