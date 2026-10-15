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
#ifndef HEDGR_RUNTIME_MANAGER_INSTANCE_MANAGER_HPP
#define HEDGR_RUNTIME_MANAGER_INSTANCE_MANAGER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace hedgr::runtime::manager {

/**
 * @brief The single writer of Hedgr's state.
 *
 * The manager owns the state tables, the store and the thread pool. The frame
 * loop calls it once per frame; it drains the commands the interface pushed,
 * applies them, then drains the events the workers pushed and applies those.
 * Nothing else may touch a table, which is the whole reason the command and
 * event channels exist.
 *
 * The class knows two kinds of failure. A recoverable one, such as a store open
 * that fails at start, is returned to the caller. A runtime one, such as a
 * failed session, is recorded as a fact about the session and surfaced in the
 * interface, because the application stays usable with a broken session.
 */
class HEDGR_RUNTIME_EXPORT instance_manager final {
public:
    /** @brief Zero threads means the executor's default pool size. */
    explicit instance_manager(std::size_t pool_threads = 0);

    ~instance_manager();

    instance_manager(const instance_manager&) = delete;
    instance_manager& operator=(const instance_manager&) = delete;

    /**
     * @brief Opens the store, loads what was saved and seeds a first run.
     *
     * Idempotent. On a database with no instances it seeds one from @p seed,
     * which the application builds from the environment, so a fresh checkout is
     * usable without typing an address. It also makes sure the Instance Manager
     * window exists, because every other window is opened from it.
     */
    [[nodiscard]] std::expected<void, std::string>
    start(const std::filesystem::path& database_path,
          const session::transport::session_options& seed);

    /** @brief Applies every buffered command, then every buffered event. */
    void pump();

    /** @brief Persists the window wall. Called when it changes and on exit. */
    [[nodiscard]] std::expected<void, std::string> save_windows();

    [[nodiscard]] const model::state::instance_table& instances() const noexcept {
        return instances_;
    }

    [[nodiscard]] const model::state::account_table& accounts() const noexcept {
        return accounts_;
    }

    [[nodiscard]] const model::state::session_table& sessions() const noexcept {
        return sessions_;
    }

    [[nodiscard]] const model::state::window_table& windows() const noexcept {
        return windows_;
    }

    /** @brief Where the interface pushes its commands. */
    [[nodiscard]] model::command::command_queue& commands() noexcept {
        return commands_;
    }

    /** @brief Where the workers push their events. */
    [[nodiscard]] model::command::event_queue& events() noexcept {
        return events_;
    }

    /** @brief The last store or startup failure, for the interface to show. */
    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] bool is_started() const noexcept { return started_; }

    /** @brief Stops every session and joins the pool. Idempotent. */
    void stop() noexcept;

private:
    void handle(const model::command::command& cmd);
    void apply(const model::event::event& ev);

    void do_save_instance(const model::command::command& cmd);
    void do_remove_instance(const model::command::command& cmd);
    void do_save_account(const model::command::command& cmd);
    void do_remove_account(const model::command::command& cmd);
    void do_connect_session(const model::command::command& cmd);
    void do_disconnect_session(const model::command::command& cmd);
    void do_open_window(const model::command::command& cmd);
    void do_close_window(const model::command::command& cmd);
    void do_refresh_window(const model::command::command& cmd);

    void seed_first_run();
    void ensure_instance_manager_window();
    void stop_session(const std::string& window_or_session_id);

    async::executor pool_;
    model::state::instance_table instances_;
    model::state::account_table accounts_;
    model::state::session_table sessions_;
    model::state::window_table windows_;
    model::command::command_queue commands_;
    model::command::event_queue events_;

    std::unique_ptr<store::sqlite::database> database_;
    std::unique_ptr<store::repository::instance_repository> instance_repo_;
    std::unique_ptr<store::repository::account_repository> account_repo_;
    std::unique_ptr<store::repository::window_repository> window_repo_;

    std::unordered_map<std::string, std::shared_ptr<std::stop_source>> running_;

    /**
     * @brief The options the process was seeded with.
     *
     * An instance row carries what differs between deployments: the address,
     * the prefix and the wire format. The TLS material and the timeouts
     * describe this machine's client instead, so they live here and every
     * session is built from them.
     */
    session::transport::session_options seed_options_;

    std::string last_error_;
    std::uint64_t next_window_serial_ = 1;
    bool started_ = false;
};

}

#endif
