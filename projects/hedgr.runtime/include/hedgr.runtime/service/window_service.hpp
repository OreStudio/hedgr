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
#ifndef HEDGR_RUNTIME_SERVICE_WINDOW_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_WINDOW_SERVICE_HPP

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.runtime/service/window_traits.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hedgr::runtime::service {

/**
 * @brief The stable id of the window showing one subject, or empty.
 *
 * A window whose id is a function of what it shows is opened by looking it up
 * rather than by making another: "one accounts window per instance" and "one
 * workspace per session" are then answered by the id, and a second request
 * focuses the window that exists. CRM windows also carry the party and CRM
 * name, because the same map differs between them. The kinds with no such
 * identity -- a chart, a grid, a notice -- have no key, and a request for one
 * always makes a new window, because two charts of two series are two windows.
 */
[[nodiscard]] std::string window_key_for(model::domain::window_kind kind,
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id = {}, std::string_view crm_name = {},
    std::string_view party_id = {});

/** @brief A read one window asked for, which the facade routes to its starter. */
struct read_request final {
    read_kind kind;
    model::state::window_handle window;
};

/** @brief What is left for the facade after an open: a read to start, a refusal to raise. */
struct open_outcome final {
    std::optional<read_request> read;
    std::optional<refusal> refused;
};

/**
 * @brief The window wall: which windows exist, what each shows, which session
 * each is bound to, and when each refreshes.
 *
 * Reads the session table and never writes it. Runs on the frame thread. It
 * starts no read of its own: a refresh or an open answers with the read it
 * needs, and the facade starts it, so the wall stays free of the transports.
 */
class window_service final {
public:
    /**
     * @param series the table whose watchers the open windows are. A window
     * that shows a series interns it and acquires it, and gives it back when
     * it closes, so the table's watcher count is the number of windows
     * holding a row.
     */
    window_service(const model::state::session_table& sessions,
        model::state::series_table& series);

    [[nodiscard]] const model::state::window_table& windows() const noexcept {
        return windows_;
    }

    /** @brief Whether a store is attached, which every save needs. */
    [[nodiscard]] bool has_store() const noexcept { return window_repo_ != nullptr; }

    /** @brief Opens the wall's repository on the store. */
    void attach(store::sqlite::database& database);

    /** @brief Replaces the wall with the one the store holds. Needs a store. */
    [[nodiscard]] std::expected<void, std::string> load();

    void ensure_instance_manager_window();

    /** @brief Marks an existing window open. False when there is none. */
    [[nodiscard]] bool show_existing(std::string_view window_id);

    [[nodiscard]] open_outcome open(const model::command::command& cmd);

    /** @brief Opens the window a sign-in lands on. */
    [[nodiscard]] open_outcome open_landing(std::string_view instance_id,
        std::string_view account_id, model::domain::window_kind kind);

    [[nodiscard]] std::optional<refusal> close(const model::command::command& cmd);

    /**
     * @brief Moves the window's deadline and says what to read.
     *
     * Nothing when there is no such window.
     */
    [[nodiscard]] std::optional<read_request>
    refresh(const model::command::command& cmd);

    /** @brief Points a CRM window at another matrix. True when it changed one. */
    [[nodiscard]] bool configure_crm(const model::command::command& cmd);

    /**
     * @brief Binds every restored window saved against one session pair.
     *
     * A window wall is restored before any session exists, so its windows are
     * unbound. The sign-in that creates the pair is the moment they can be
     * bound, and the refresh deadlines move to now so each reads its data
     * immediately rather than waiting a cadence.
     */
    void rebind(std::string_view instance_id, std::string_view account_id,
        model::state::session_handle session);

    /**
     * @brief Unbinds every window bound to one session.
     *
     * Called before the session row is released, because release bumps the
     * generation and the handle a window holds would stop comparing equal to
     * the row it points at. The windows stay open and draw the session-loss
     * state.
     */
    void unbind(model::state::session_handle session);

    /**
     * @brief Makes every window of one instance stop holding its series.
     *
     * An empty @p account_id means every account of the instance. The windows
     * stay open and keep their place on the wall. Called before the rows are
     * forgotten, because a handle is read through its row.
     */
    void release_scope(std::string_view instance_id,
        std::string_view account_id);

    /** @brief Writes every window. The refusal to raise, or nothing. Needs a store. */
    [[nodiscard]] std::optional<refusal> save_windows();

private:
    /**
     * @brief Makes the window hold exactly the series its state names.
     *
     * Idempotent. A series already held keeps its watcher, so a window that is
     * restored, rebound and reopened holds each series once. The scope is the
     * session pair the window shows, which a restored window has before its
     * session signs in.
     */
    void hold_series(model::state::window_handle window,
        std::string_view instance_id, std::string_view account_id);

    /** @brief Gives back every series the window holds. */
    void drop_series(model::state::window_handle window);

    const model::state::session_table& sessions_;
    model::state::series_table& series_;
    model::state::window_table windows_;
    std::unique_ptr<store::repository::window_repository> window_repo_;
    std::uint64_t next_window_serial_ = 1;
};

}

#endif
