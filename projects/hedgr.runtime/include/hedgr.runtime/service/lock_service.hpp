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
#ifndef HEDGR_RUNTIME_SERVICE_LOCK_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_LOCK_SERVICE_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/master_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <boost/asio/io_context.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace hedgr::runtime::service {

/** @brief Whether @p kind is one of the three events the lock applies. */
[[nodiscard]] bool is_lock_event(model::event::event_kind kind) noexcept;

/**
 * @brief A credential that would not open during a connect.
 *
 * A fact about that account's session and not about the lock, so the lock
 * does not act on it and the facade hands it to supervision.
 */
struct credential_failure final {
    std::string instance_id;
    std::string account_id;
    std::string message;
};

/** @brief What is left for the facade after the lock applied an event. */
struct lock_effect final {
    /** @brief The store was unlocked, so the refusals raised against the lock are stale. */
    bool unlocked = false;
    std::optional<credential_failure> credential;
};

/**
 * @brief The store lock: the master password, whether it is held, and the
 * sealed credentials that depend on it.
 *
 * Runs on the frame thread and is the only writer of its state. The pool only
 * derives and seals: each coroutine pushes its result as an event or a command
 * and the frame thread applies it here, so no pool thread reads or writes a
 * member of this class.
 */
class lock_service final {
public:
    lock_service(boost::asio::io_context& pool,
        model::command::command_queue& commands,
        model::command::event_queue& events,
        model::state::account_table& accounts);

    /** @brief Where the master password stands. */
    [[nodiscard]] model::domain::store_lock store_lock() const noexcept {
        return store_lock_;
    }

    /** @brief Whether a derivation is running on the pool right now. */
    [[nodiscard]] bool is_unlocking() const noexcept { return unlocking_; }

    /** @brief Why the last unlock was refused, or empty. */
    [[nodiscard]] const std::string& lock_error() const noexcept {
        return lock_error_;
    }

    /**
     * @brief The held master password, empty unless the store is unlocked.
     *
     * Read-only, for the sign-in that opens one stored credential with it.
     */
    [[nodiscard]] const model::domain::secret_string& master_password() const noexcept {
        return master_password_;
    }

    /** @brief Opens the master repository on the store and keeps the account repository. */
    void attach(store::sqlite::database& database,
        store::repository::account_repository& accounts);

    /** @brief Reads whether the store has a master password. Needs a store. */
    [[nodiscard]] std::expected<void, std::string> load();

    [[nodiscard]] std::optional<refusal>
    set_master_password(const model::command::command& cmd);

    [[nodiscard]] std::optional<refusal>
    unlock_store(const model::command::command& cmd);

    /**
     * @brief Forgets the held password and returns to @c locked.
     *
     * Always lands on @c locked and never on @c failed. Locking is the
     * operator's interest and not a check that failed, which is the whole
     * reason the two states exist.
     */
    void lock_store();

    /** @brief Writes one sealed credential and marks the account as holding it. */
    [[nodiscard]] std::optional<refusal>
    store_credential(const model::command::command& cmd);

    /**
     * @brief Seals a password and sends it back to be stored.
     *
     * The key derivation is deliberate work, so it runs on the pool and the
     * sealed value returns as a command the frame loop applies. Both secrets
     * are wiped when the coroutine ends.
     */
    void remember(std::string account_id, std::string password);

    /** @brief Applies one of the three lock events. See @c is_lock_event. */
    [[nodiscard]] lock_effect apply(const model::event::event& ev);

private:
    void drop_master_password(bool deliberate);

    boost::asio::io_context& pool_;
    model::command::command_queue& commands_;
    model::command::event_queue& events_;
    model::state::account_table& accounts_;
    store::repository::account_repository* account_repo_ = nullptr;
    std::unique_ptr<store::repository::master_repository> master_repo_;

    /**
     * @brief The master password, held only while the store is unlocked.
     *
     * A @c secret_string and not a @c std::string, because this one has to
     * survive between frames: it is the copy most likely to be forgotten, and
     * the type is what stops it being copied somewhere else and wipes the bytes
     * when the lock closes. It is never logged, never placed on an event and
     * never written to a window's state; the only reader is the seal and the
     * session job that opens one credential with it.
     */
    model::domain::secret_string master_password_;

    model::domain::store_lock store_lock_ = model::domain::store_lock::absent;

    bool unlocking_ = false;

    std::string lock_error_;
};

}

#endif
