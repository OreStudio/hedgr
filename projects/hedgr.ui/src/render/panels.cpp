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
#include "hedgr.ui/render/panels.hpp"

#include <format>
#include <string>
#include <string_view>
#include <variant>

namespace {

namespace domain = hedgr::model::domain;
using hedgr::ui::render::render_input;

/** @brief An instance's name by key, falling back to the key itself. */
[[nodiscard]] std::string instance_name(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.instances.find(id))
        return input.instances.get(*handle)->name;
    return std::string{id};
}

/** @brief An account's name by key, falling back to the key itself. */
[[nodiscard]] std::string account_name(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.accounts.find(id)) {
        const auto* row = input.accounts.get(*handle);
        return row->name.empty() ? row->principal : row->name;
    }
    return std::string{id};
}

/** @brief An account's principal by key, which is what a workspace titles. */
[[nodiscard]] std::string account_principal(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.accounts.find(id))
        return input.accounts.get(*handle)->principal;
    return std::string{id};
}

}

namespace hedgr::ui::render {

std::string window_title(domain::window_kind kind,
    const domain::window_state& state, const render_input& input) {
    /*
     * Read one alternative at a time rather than switching over the kind and
     * then reading the state: the kind and the state's alternative are set from
     * the same command and cannot disagree for a window that came through the
     * store, but a window built by hand in a test can, and a title function
     * should report what the state says rather than crash on the mismatch.
     */
    if (const auto* chart = std::get_if<domain::chart_state>(&state)) {
        if (kind == domain::window_kind::chart)
            return chart->label.empty() ? std::string{"Chart"} : chart->label;
    }
    if (const auto* grid = std::get_if<domain::grid_state>(&state)) {
        if (kind == domain::window_kind::grid)
            return grid->resource.empty() ? std::string{"Grid"} : grid->resource;
    }
    if (const auto* notice = std::get_if<domain::notice_state>(&state)) {
        if (kind == domain::window_kind::notice)
            return notice->title.empty() ? std::string{"Notice"} : notice->title;
    }
    if (const auto* accounts = std::get_if<domain::accounts_state>(&state)) {
        if (kind == domain::window_kind::accounts) {
            return std::format("Accounts: {}",
                instance_name(input, accounts->instance_id));
        }
    }
    if (const auto* session = std::get_if<domain::workspace_state>(&state)) {
        if (kind == domain::window_kind::workspace) {
            /*
             * The title names the principal, because a wall may hold several
             * workspaces over the same instance and who is signed in is the
             * only thing that tells them apart. The instance follows it from
             * the session the window is bound to, which the frame loop knows
             * and this function does not: see the renderer, which appends it.
             */
            return account_principal(input, session->account_id);
        }
    }

    switch (kind) {
    case domain::window_kind::chart:
        return "Chart";
    case domain::window_kind::grid:
        return "Grid";
    case domain::window_kind::notice:
        return "Notice";
    case domain::window_kind::instance_manager:
        return "Instances";
    case domain::window_kind::accounts:
        return "Accounts";
    case domain::window_kind::workspace:
        return "Workspace";
    }
    return "Window";
}

bool shows_instances(const render_input& input) noexcept {
    return input.store_lock == domain::store_lock::unlocked;
}

bool shows_window(domain::window_kind kind, const render_input& input,
    std::string_view window_id) noexcept {
    static_cast<void>(window_id);

    if (shows_instances(input))
        return true;

    /*
     * A closed lock does not hide everything: a notice, a chart and a grid are
     * surfaces whose contents arrive over a session rather than out of the
     * store, and a session is not encrypted by the master password. What the
     * lock closes is the catalogue -- the accounts windows and the workspaces
     * -- because every one of those reads rows that only the master password
     * opens.
     *
     * The Instances window stays. It is the shell the lock control lives on,
     * and it is the surface an operator is looking at when they decide to
     * open the store; taking it away would leave them with a program that had
     * apparently stopped being the one they were using. Its rows are blanked
     * instead, by the renderer that knows which part of it is catalogue.
     */
    switch (kind) {
    case domain::window_kind::chart:
    case domain::window_kind::grid:
    case domain::window_kind::notice:
    case domain::window_kind::instance_manager:
        return true;
    case domain::window_kind::accounts:
    case domain::window_kind::workspace:
        return false;
    }
    return false;
}

startup_notice startup_notice_for(std::string_view startup_failure) {
    /*
     * Empty in, empty out. There is nothing to warn about, and the frame reads
     * @c empty as the instruction not to draw the notice at all, so a healthy
     * store draws exactly what it drew before this existed.
     */
    if (startup_failure.empty())
        return startup_notice{};

    /*
     * The heading names the fault and the reason below it is the runtime's own
     * sentence, unchanged. The store whose schema version is refused is the one
     * that reaches an operator: its sentence names the file, both versions and
     * the remedy, and restating any of that here would be a second copy of the
     * store's diagnosis for the two to drift apart.
     */
    return startup_notice{
        .heading = "Hedgr could not open its store.",
        .reason = std::string{startup_failure},
    };
}

}
