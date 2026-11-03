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
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/state/condition_table.hpp"
#include "hedgr.model/state/password_policy_state.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.model/state/tenant_image_map.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.model/state/term_structure_table.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/service/catalogue_service.hpp"
#include "hedgr.runtime/service/lock_service.hpp"
#include "hedgr.runtime/service/market_feed.hpp"
#include "hedgr.runtime/service/password_policy_service.hpp"
#include "hedgr.runtime/service/probe_service.hpp"
#include "hedgr.runtime/service/read_service.hpp"
#include "hedgr.runtime/service/session_service.hpp"
#include "hedgr.runtime/service/window_service.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
    /**
     * @brief Zero threads means the executor's default pool size.
     *
     * @p market_feeds makes the live price feed of each authenticated session.
     * Empty means a real @c session_actor; a test passes a fake.
     */
    explicit instance_manager(std::size_t pool_threads = 0,
        worker::tick_feed_factory market_feeds = {});

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
     *
     * Reports the lock in @c store_lock(), which the interface gates the whole
     * of its catalogue on: a store with a master row starts @c locked and one
     * without starts @c absent. Nothing here reads a credential, so a store
     * that never gets unlocked still starts.
     */
    [[nodiscard]] std::expected<void, std::string>
    start(const std::filesystem::path& database_path,
          const session::transport::session_options& seed);

    /** @brief Applies every buffered command, then every buffered event. */
    void pump();

    /**
     * @brief Makes sure the accounts window of @p instance_id exists and is
     * open, and returns its stable id.
     *
     * Idempotent, because the rule is one accounts window per instance: the
     * second request finds the first rather than making a second, which is what
     * keeps a restored wall from holding two windows over one instance's
     * accounts. The interface could not do this itself -- reading a window's
     * state to find out which instance it belongs to is not something the
     * render path should have to do on every button press -- so the decision
     * lives here, beside the table it changes.
     */
    [[nodiscard]] std::string open_accounts_window(
        const std::string& instance_id);

    /** @brief Persists the window wall. Called when it changes and on exit. */
    [[nodiscard]] std::expected<void, std::string> save_windows();

    [[nodiscard]] const model::state::instance_table& instances() const noexcept {
        return catalogue_.instances();
    }

    [[nodiscard]] const model::state::account_table& accounts() const noexcept {
        return catalogue_.accounts();
    }

    [[nodiscard]] const model::state::folder_table& folders() const noexcept {
        return catalogue_.folders();
    }

    [[nodiscard]] const model::state::label_table& labels() const noexcept {
        return catalogue_.labels();
    }

    [[nodiscard]] const model::state::labelling_table& labellings() const
        noexcept {
        return catalogue_.labellings();
    }

    /**
     * @brief The shortcuts bar, in the order it draws in.
     *
     * The interface reads this to draw the bar. It is loaded once at start and
     * then written only by the commands that add, remove or drop an entry, so a
     * caller never sorts it and never sees an entry the store refused.
     */
    [[nodiscard]] const model::state::shortcut_table& shortcuts() const
        noexcept {
        return catalogue_.shortcuts();
    }

    [[nodiscard]] const model::state::session_table& sessions() const noexcept {
        return sessions_;
    }

    /**
     * @brief The probes' last results and next deadlines, per instance.
     *
     * Read by the frame loop only, and written by it only: a probe reports
     * through the event channel like every other worker, so this table has the
     * single writer the concurrency model requires.
     */
    [[nodiscard]] const model::state::probe_table& probes() const noexcept {
        return probe_service_.probes();
    }

    [[nodiscard]] const model::state::window_table& windows() const noexcept {
        return window_service_.windows();
    }

    /** @brief The market series, their catalogues and the points each holds. */
    [[nodiscard]] const model::state::series_table& market() const noexcept {
        return series_;
    }

    /** @brief Closed buckets dropped for a series released in flight. */
    [[nodiscard]] std::size_t stale_buckets() const noexcept {
        return market_feed_.stale_buckets();
    }

    /** @brief Closed buckets dropped for being older than the newest held. */
    [[nodiscard]] std::size_t late_buckets() const noexcept {
        return market_feed_.late_buckets();
    }

    /** @brief Backfill answers dropped for a series nobody holds. */
    [[nodiscard]] std::size_t unheld_points() const noexcept {
        return read_service_.unheld_points();
    }

    /** @brief Which image each tenant currency names. */
    [[nodiscard]] const model::state::tenant_image_map&
    currency_images() const noexcept {
        return read_service_.currency_images();
    }

    /** @brief The latest CRM batch each session and party read. */
    [[nodiscard]] const model::state::crm_table& crm() const noexcept {
        return read_service_.crm();
    }

    /** @brief The latest curve each browser session read. */
    [[nodiscard]] const model::state::term_structure_table&
    term_structures() const noexcept {
        return read_service_.term_structures();
    }

    /** @brief The bounded cache of tenant images by identifier. */
    [[nodiscard]] model::state::image_cache& images() noexcept {
        return read_service_.images();
    }
    [[nodiscard]] const model::state::image_cache& images() const noexcept {
        return read_service_.images();
    }

    /** @brief Images whose reads failed, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    failed_images() const noexcept {
        return read_service_.failed_images();
    }

    /** @brief Images whose reads are in flight, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    pending_images() const noexcept {
        return read_service_.pending_images();
    }

    /**
     * @brief What the deployment said a new password must satisfy.
     *
     * Read by the create prompt, which is drawn while the store is still shut
     * and therefore has no instance row to look a policy up on: the rules
     * belong to the deployment the process was pointed at. While the state is
     * not @c loaded the screen must say strength cannot be assessed rather
     * than assess against nothing, because a policy of zeroes is a claim and
     * the absence of a policy is not.
     */
    [[nodiscard]] const model::state::password_policy_state&
    password_policy() const noexcept {
        return policy_service_.password_policy();
    }

    /** @brief Where the interface pushes its commands. */
    [[nodiscard]] model::command::command_queue& commands() noexcept {
        return commands_;
    }

    /** @brief Where the workers push their events. */
    [[nodiscard]] model::command::event_queue& events() noexcept {
        return events_;
    }

    /**
     * @brief Every failure the runtime has raised and not yet seen resolved.
     *
     * Keyed by cause, so a reader that wants more than one line, or the
     * condition about one window, finds it here. @c last_error is the one-line
     * view of the same table.
     */
    [[nodiscard]] const model::state::condition_table&
    conditions() const noexcept {
        return conditions_;
    }

    /**
     * @brief The one failure a single-line surface shows, or empty.
     *
     * The gravest condition in the table, and the newest among those, which
     * is the rule @c condition_table::current pins. The reference is to the
     * table's row and holds until the next raise or clear on the frame thread.
     */
    [[nodiscard]] const std::string& last_error() const noexcept {
        static const std::string none;
        const auto* shown = conditions_.current();
        return shown == nullptr ? none : shown->message;
    }

    /**
     * @brief Why the store could not be brought up, or empty.
     *
     * Kept apart from @c last_error, which is the newest of the conditions a
     * failed command raises and the next command takes down: that one is the
     * runtime's running note about whatever it last did, and this one is the
     * reason this process has no store at all. Both are worth reading, and
     * only this one decides whether the interface draws the startup notice, so
     * a command that fails later cannot be shown as a store that never opened.
     *
     * Nothing clears it. A process whose store was refused has no way back --
     * the remedy is to delete the file and start again -- so a notice that
     * later activity could clear would be reporting a state this process
     * cannot leave.
     */
    [[nodiscard]] const std::string& startup_failure() const noexcept {
        return startup_failure_;
    }

    /**
     * @brief Where the master password stands.
     *
     * The interface gates every catalogue surface on this: while it is not
     * @c unlocked the store cannot be read, because every read is a decryption
     * of something only the master password opens. @c absent and @c locked are
     * therefore states to draw and not errors to report -- @c failed is the
     * only one that describes an attempt.
     */
    [[nodiscard]] model::domain::store_lock store_lock() const noexcept {
        return lock_service_.store_lock();
    }

    /**
     * @brief Whether an unlock is being derived right now.
     *
     * The derivation is deliberately slow, so a prompt that shows nothing for
     * half a second reads as a hung application. This is what lets it say it is
     * working instead.
     */
    [[nodiscard]] bool is_unlocking() const noexcept {
        return lock_service_.is_unlocking();
    }

    /**
     * @brief Why the last unlock was refused, or empty.
     *
     * Separate from @c last_error because the two go to different places: a
     * refused password is shown inline beside the prompt that asked for it,
     * while a store failure is a banner. Folding them would either put an
     * infrastructure failure in a password box or a typo in a banner, and both
     * send the operator to the wrong place.
     *
     * Never a secret: the sentence says the password was not accepted and
     * nothing about what was tried.
     */
    [[nodiscard]] const std::string& lock_error() const noexcept {
        return lock_service_.lock_error();
    }

    [[nodiscard]] bool is_started() const noexcept { return started_; }

    /** @brief Stops every session and joins the pool. Idempotent. */
    void stop() noexcept;

private:
    /**
     * @brief Applies one command, then wipes the password it carried.
     *
     * A mutable reference rather than a const one for exactly that reason: the
     * command is drained into a local vector that dies with the frame, and a
     * secret in a buffer nobody wipes is a secret left for the next allocation.
     */
    void handle(model::command::command& cmd);
    void apply(const model::event::event& ev);

    /**
     * @brief Records @p message as an error condition for @p key, now.
     *
     * The one call every failure in this class goes through. A repeat of the
     * same cause updates its row, and the sentence is the one the operator
     * reads, so it says what went wrong and what to do about it.
     */
    void raise(model::state::condition_key key, std::string message);

    void do_refresh_window(const model::command::command& cmd);

    /**
     * @brief Raises what the session service refused, then does what it asked.
     *
     * The session service decides what happens to a session; the windows, the
     * feed and the reads belong to other services, so it describes them and
     * this performs them.
     */
    void complete_session(service::session_effect effect);

    /** @brief Starts one read a session's fact asked for, on its own actor. */
    void start_session_read(const service::session_effect::read_start& read);

    /** @brief Starts the read an open asked for, then raises what it refused. */
    void complete_open(service::open_outcome outcome);

    /**
     * @brief Raises what the catalogue refused, then winds down what a removal
     * left behind.
     *
     * The catalogue owns the rows and returns what it removed. The session, the
     * probe, the stream and the windows of a removed instance or account belong
     * to other jobs, so this is the one place that reaches them.
     */
    void complete_catalogue(service::catalogue_effect effect);

    /** @brief Routes a window's read request to the starter that serves it. */
    void start_read(const service::read_request& request);

    /**
     * @brief Stops and forgets one session's actor, without saying goodbye.
     *
     * A teardown: the session row is going, or the process is, so the instance
     * is not asked to drop a token nobody will use again.
     */
    void retire_session(std::string_view instance_id,
                        std::string_view account_id);

    /**
     * @brief Posts the fact that a store operation was refused.
     *
     * Used by the paths that run on the pool, which must not touch the state
     * tables: the frame loop applies the fact and the refusal reads to the
     * operator exactly as it would had it been decided in @c handle.
     */
    void report_store_failure(const std::string& instance_id,
                              const std::string& account_id,
                              std::string message);

    async::executor pool_;

    /**
     * @brief The pool the probes run on, separate from the sessions'.
     *
     * Deliberately not the session pool. ADR 2 sizes that pool as the number
     * of round trips an operator may have in flight, and a probe occupies a
     * thread for the length of a connect timeout; polling every instance on
     * that pool would spend the operator's budget on background work and
     * would slow down the very sessions the polling exists to leave alone.
     * Two threads bound how many connects are in flight at once, which is the
     * honest cost of a blocking transport.
     */
    async::executor probe_pool_;

    service::catalogue_service catalogue_;
    model::state::session_table sessions_;
    model::command::command_queue commands_;
    model::command::event_queue events_;

    std::unique_ptr<store::sqlite::database> database_;

    /**
     * @brief Pairs whose catalogue must be read again after a party change.
     *
     * A catalogue read that was already in flight holds the pending mark, so
     * the new read would be dropped. The pair is remembered here and the read
     * is started when that answer lands.
     */
    std::unordered_set<std::string> catalogue_rereads_;

    /**
     * @brief The options the process was seeded with.
     *
     * An instance row carries what differs between deployments: the address,
     * the prefix, the wire format, and the TLS material when the operator has
     * given that instance its own. This is the fallback for every path a row
     * leaves empty, and the whole answer for the one read that has no row to
     * look at -- the password policy, which is asked before the store is open.
     * The timeouts describe this machine's client and come from here alone.
     */
    session::transport::session_options seed_options_;

    /** @brief The failures raised and not yet resolved. See @c conditions(). */
    model::state::condition_table conditions_;

    /** @brief Why the store could not be brought up. See @c startup_failure(). */
    std::string startup_failure_;

    bool started_ = false;

    /*
     * Last, so both are built after and destroyed before the tables, the
     * channel and the options they hold references to.
     */
    service::probe_service probe_service_{catalogue_.instances(), probe_pool_.context(), events_};
    service::password_policy_service policy_service_{seed_options_,
        probe_pool_.context(), events_};
    /*
     * The one series table. The windows hold its rows, the read service
     * writes the catalogues and backfills, and the feed writes the buckets.
     */
    model::state::series_table series_;
    service::window_service window_service_{sessions_, series_};
    service::lock_service lock_service_{pool_.context(), commands_, events_, catalogue_.accounts()};
    /**
     * @brief The session service, which owns every session's actor and row.
     *
     * The tables and the two lock getters are handed over by reference, as the
     * plan says; everything else it needs to reach it returns as an effect.
     */
    service::session_service session_service_{events_,
        service::session_dependencies{
            .instances = catalogue_.instances(),
            .accounts = catalogue_.accounts(),
            .sessions = sessions_,
            .seed = seed_options_,
            .database_path = [this]() -> std::filesystem::path {
                return database_ ? database_->path()
                                 : std::filesystem::path{};
            },
            .sealed_credential =
                [this](std::string_view account_id)
                -> std::expected<std::optional<std::string>,
                    store::sqlite::store_error> {
                return catalogue_.account_repository().credential_of(
                    std::string{account_id});
            },
            .store_lock = [this] { return lock_service_.store_lock(); },
            .master_password = [this] { return lock_service_.master_password(); },
            .remember = [this](std::string_view account_id,
                            std::string password) {
                lock_service_.remember(std::string{account_id},
                    std::move(password));
            },
        }};
    service::read_service read_service_{sessions_,
        window_service_.windows(), series_,
        [this](std::string_view instance_id, std::string_view account_id,
               const worker::read_request& request) {
            return session_service_.submit_read(instance_id, account_id,
                request);
        }};
    service::market_feed market_feed_{sessions_, series_, session_service_,
        [this](std::string_view instance_id, std::string_view account_id) {
            return read_service_.start_market_catalogue_read(instance_id,
                account_id);
        },
        [this](std::string_view instance_id, std::string_view account_id,
               std::string_view series_id) {
            read_service_.start_market_backfill(instance_id, account_id,
                series_id);
        }};
};

}

#endif
