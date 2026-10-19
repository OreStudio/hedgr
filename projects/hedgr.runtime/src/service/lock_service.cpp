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
#include "hedgr.runtime/service/lock_service.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <format>
#include <string>
#include <utility>

namespace hedgr::runtime::service {

namespace {

namespace command = hedgr::model::command;
namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace model_state = hedgr::model::state;
namespace store_crypto = hedgr::store::crypto;

using condition_source = model_state::condition_source;
using support::lg;
using support::store_failure;

}

bool is_lock_event(model::event::event_kind kind) noexcept {
    return kind == model::event::event_kind::master_sealed
        || kind == model::event::event_kind::store_unlocked
        || kind == model::event::event_kind::store_lock_failed;
}

lock_service::lock_service(boost::asio::io_context& pool,
    model::command::command_queue& commands, model::command::event_queue& events,
    model::state::account_table& accounts)
    : pool_(pool), commands_(commands), events_(events), accounts_(accounts) {
}

void lock_service::attach(store::sqlite::database& database,
    store::repository::account_repository& accounts) {
    account_repo_ = &accounts;
    master_repo_ = std::make_unique<store::repository::master_repository>(
        database);
}

std::expected<void, std::string> lock_service::load() {
    auto has_master = master_repo_->has_master();
    if (!has_master) {
        return std::unexpected(store_failure("cannot read the store's master password",
            has_master.error()));
    }
    store_lock_ = has_master.value() ? model::domain::store_lock::locked
                                     : model::domain::store_lock::absent;
    return {};
}

std::optional<refusal>
lock_service::store_credential(const command::command& cmd) {
    if (!account_repo_) {
        return refusal{{condition_source::credential, cmd.account_id},
            "cannot store a password: the store is not open"};
    }
    if (cmd.payload.empty()) {
        return refusal{{condition_source::credential, cmd.account_id},
            "cannot store an empty credential"};
    }

    if (auto saved = account_repo_->set_credential(cmd.account_id, cmd.payload);
        !saved) {
        return refusal{{condition_source::credential, cmd.account_id},
            store_failure(
                std::format("cannot store the password for account {}",
                    cmd.account_id),
                saved.error())};
    }

    if (const auto handle = accounts_.find(cmd.account_id)) {
        auto row = *accounts_.get(*handle);
        row.has_credential = true;
        static_cast<void>(accounts_.upsert(row));
    }

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Stored a password for account '" << cmd.account_id << "'";
    return std::nullopt;
}

void lock_service::remember(std::string account_id,
    std::string password) {
    auto master_password = master_password_;
    boost::asio::co_spawn(pool_,
        [this, account_id = std::move(account_id),
            password = std::move(password),
            master_password = std::move(master_password)]() mutable
        -> boost::asio::awaitable<void> {
            co_await boost::asio::post(co_await boost::asio::this_coro::executor,
                boost::asio::use_awaitable);

            auto sealed =
                store_crypto::secret_box::seal(master_password.view(), password);

            std::fill(password.begin(), password.end(), '\0');
            password.clear();
            password.shrink_to_fit();
            master_password.clear();

            if (!sealed) {
                BOOST_LOG_SEV(lg(), ores::logging::error)
                    << "Could not seal the password for account '"
                    << account_id << "'";
                co_return;
            }

            command::command store;
            store.kind = command::command_kind::store_credential;
            store.account_id = account_id;
            store.payload = std::move(sealed).value();
            commands_.push(std::move(store));
        },
        boost::asio::detached);
}

std::optional<refusal>
lock_service::set_master_password(const command::command& cmd) {
    if (!master_repo_) {
        return refusal{{condition_source::store_lock, {}},
            "cannot set a master password: the store is not open"};
    }
    if (unlocking_) {
        return refusal{{condition_source::store_lock, {}},
            "cannot set a master password: an unlock is already running"};
    }

    /*
     * Refused when one already exists. Replacing it would be worse than
     * useless: the credentials in the store were sealed under the old password,
     * so the store would look healthy and open nothing.
     */
    auto existing = master_repo_->has_master();
    if (!existing) {
        return refusal{{condition_source::store_lock, {}},
            store_failure("cannot read the store's master password",
                existing.error())};
    }
    if (existing.value()) {
        return refusal{{condition_source::store_lock, {}},
            
                "this store already has a master password; unlock it instead"};
    }

    if (cmd.password.empty()) {
        return refusal{{condition_source::store_lock, {}},
            "the master password cannot be empty"};
    }

    /*
     * The sealing is a derivation and therefore slow, so it happens on the pool
     * like every other slow call. The pool only seals: the row is written and
     * the password held when @c master_sealed is applied on the frame thread.
     */
    auto password = cmd.password;

    unlocking_ = true;
    boost::asio::co_spawn(pool_,
        [this, password = std::move(password)]() mutable
        -> boost::asio::awaitable<void> {
            co_await boost::asio::post(co_await boost::asio::this_coro::executor,
                boost::asio::use_awaitable);

            auto sealed = store::repository::master_repository::seal(password.view());

            event::event fact;
            if (!sealed) {
                fact.kind = event::event_kind::store_lock_failed;
                fact.message = store_failure(
                    "the master password could not be created", sealed.error());
            } else {
                fact.kind = event::event_kind::master_sealed;
                fact.payload = std::move(sealed).value();
                fact.master_password = std::move(password);
            }
            events_.push(std::move(fact));
        },
        boost::asio::detached);
    return std::nullopt;
}

std::optional<refusal>
lock_service::unlock_store(const command::command& cmd) {
    if (!master_repo_) {
        return refusal{{condition_source::store_lock, {}},
            "cannot unlock: the store is not open"};
    }
    if (unlocking_) {
        return refusal{{condition_source::store_lock, {}},
            "cannot unlock: an unlock is already running"};
    }

    if (cmd.password.empty()) {
        lock_error_ = "no master password was supplied";
        store_lock_ = domain::store_lock::failed;
        return std::nullopt;
    }

    /*
     * The row is read here, on the frame loop, because it is a cheap query, and
     * the derivation that uses it runs on the pool. Reading it here also means
     * the blob is taken from the store as it is now rather than as it was when
     * the coroutine happened to start.
     */
    auto blob = master_repo_->verification();
    if (!blob) {
        return refusal{{condition_source::store_lock, {}},
            store_failure("cannot read the master password",
                blob.error())};
    }
    if (blob.value().empty()) {
        store_lock_ = domain::store_lock::absent;
        lock_error_ = "this store has no master password yet";
        return std::nullopt;
    }

    auto password = cmd.password;
    const auto sealed = std::move(blob).value();

    /*
     * The previous refusal is cleared as the new attempt starts, so the prompt
     * does not show last time's sentence beside this time's password.
     */
    lock_error_.clear();
    unlocking_ = true;
    boost::asio::co_spawn(pool_,
        [this, password = std::move(password), sealed]() mutable
        -> boost::asio::awaitable<void> {
            co_await boost::asio::post(co_await boost::asio::this_coro::executor,
                boost::asio::use_awaitable);

            auto opened = store_crypto::secret_box::open(password.view(), sealed);

            event::event fact;
            if (opened) {
                fact.kind = event::event_kind::store_unlocked;
                fact.lock = domain::store_lock::unlocked;

                /*
                 * The plaintext that came out of the verification blob is the
                 * known sentence and is not needed; it is dropped here, and the
                 * derivation's own buffers are wiped by @c open before it
                 * returns.
                 */
                fact.master_password = std::move(password);
            } else if (opened.error().code
                == store_crypto::box_errc::wrong_master_password) {
                /*
                 * The one refusal an operator can act on, and the one the
                 * prompt shows inline. It is not @c last_error: that field
                 * feeds a general failure banner, and a mistyped password is
                 * not a store failure.
                 */
                fact.kind = event::event_kind::store_lock_failed;
                fact.lock = domain::store_lock::failed;
                fact.message = "the master password is not correct";
            } else {
                fact.kind = event::event_kind::store_lock_failed;
                fact.message = std::format("the store could not be unlocked: {}",
                    opened.error().message);
            }
            events_.push(std::move(fact));
        },
        boost::asio::detached);
    return std::nullopt;
}

void lock_service::lock_store() {
    /*
     * Locking is the interest of the operator and never of a failure path, so
     * it always lands on @c locked. Everything sealed under the password becomes
     * unreadable the moment this returns, which is the point of the action.
     */
    lock_error_.clear();
    drop_master_password(true);
}

void lock_service::drop_master_password(bool deliberate) {
    master_password_.clear();
    store_lock_ = deliberate ? domain::store_lock::locked
                             : domain::store_lock::failed;
}

lock_effect lock_service::apply(const event::event& ev) {
    lock_effect effect;
    if (ev.kind == event::event_kind::master_sealed) {
        unlocking_ = false;
        auto stored = master_repo_->store_verification(ev.payload);
        if (!stored) {
            store_lock_ = domain::store_lock::failed;
            lock_error_ = store_failure(
                "the master password could not be created", stored.error());
            return effect;
        }
        master_password_ = ev.master_password;
        store_lock_ = domain::store_lock::unlocked;
        effect.unlocked = true;
        lock_error_.clear();
        return effect;
    }
    if (ev.kind == event::event_kind::store_unlocked) {
        /*
         * The derivation succeeded on the pool, so the password that opened it
         * is now held and every surface that reads the catalogue can be drawn.
         * It arrives on the event and is assigned here, on the frame thread,
         * which is the only one that reads the held secret.
         */
        unlocking_ = false;
        master_password_ = ev.master_password;
        store_lock_ = domain::store_lock::unlocked;
        effect.unlocked = true;
        lock_error_.clear();
        return effect;
    }
    if (ev.kind == event::event_kind::store_lock_failed) {
        /*
         * @c failed only when the attempt was an unlock, which is the one case
         * an operator is waiting on an answer. An event naming an account is a
         * credential that would not open during a connect, which is a fact
         * about that session; the lock itself has not been refused, because
         * nobody asked it to be. Leaving the lock alone there is what stops a
         * single unreadable credential from throwing the operator back to the
         * unlock prompt mid-run.
         */
        if (ev.account_id.empty()) {
            unlocking_ = false;
            store_lock_ = domain::store_lock::failed;
            /*
             * Beside the prompt and not in the banner. The distinction is the
             * reason @c lock_error exists separately from @c last_error: a
             * refusal is answered by typing something else, and a banner is
             * answered by an administrator.
             */
            lock_error_ = ev.message;
            return effect;
        }

        effect.credential =
            credential_failure{ev.instance_id, ev.account_id, ev.message};
        return effect;
    }

    return effect;
}

}
