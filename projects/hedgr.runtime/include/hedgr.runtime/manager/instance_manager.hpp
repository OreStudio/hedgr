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
#include "hedgr.model/state/market_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/service/password_policy_service.hpp"
#include "hedgr.runtime/service/probe_service.hpp"
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.runtime/worker/image_worker.hpp"
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/folder_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/label_repository.hpp"
#include "hedgr.store/repository/labelling_repository.hpp"
#include "hedgr.store/repository/master_repository.hpp"
#include "hedgr.store/repository/shortcut_repository.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
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
        return instances_;
    }

    [[nodiscard]] const model::state::account_table& accounts() const noexcept {
        return accounts_;
    }

    [[nodiscard]] const model::state::folder_table& folders() const noexcept {
        return folders_;
    }

    [[nodiscard]] const model::state::label_table& labels() const noexcept {
        return labels_;
    }

    [[nodiscard]] const model::state::labelling_table& labellings() const
        noexcept {
        return labellings_;
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
        return shortcuts_;
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
        return windows_;
    }

    /** @brief The market series and the points each chart holds. */
    [[nodiscard]] const model::state::market_table& market() const noexcept {
        return market_;
    }

    /** @brief Which image each tenant currency names. */
    [[nodiscard]] const model::state::tenant_image_map&
    currency_images() const noexcept {
        return currency_images_;
    }

    /** @brief The latest CRM batch each session and party read. */
    [[nodiscard]] const model::state::crm_table& crm() const noexcept {
        return crm_;
    }

    /** @brief The latest curve each browser session read. */
    [[nodiscard]] const model::state::term_structure_table&
    term_structures() const noexcept {
        return term_structures_;
    }

    /** @brief The bounded cache of tenant images by identifier. */
    [[nodiscard]] model::state::image_cache& images() noexcept {
        return images_;
    }
    [[nodiscard]] const model::state::image_cache& images() const noexcept {
        return images_;
    }

    /** @brief Images whose reads failed, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    failed_images() const noexcept {
        return failed_images_;
    }

    /** @brief Images whose reads are in flight, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    pending_images() const noexcept {
        return images_reading_;
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
        return store_lock_;
    }

    /**
     * @brief Whether an unlock is being derived right now.
     *
     * The derivation is deliberately slow, so a prompt that shows nothing for
     * half a second reads as a hung application. This is what lets it say it is
     * working instead.
     */
    [[nodiscard]] bool is_unlocking() const noexcept { return unlocking_; }

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
        return lock_error_;
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

    void do_save_instance(const model::command::command& cmd);
    void do_remove_instance(const model::command::command& cmd);
    void do_save_account(const model::command::command& cmd);
    void do_remove_account(const model::command::command& cmd);
    void do_save_folder(const model::command::command& cmd);
    void do_remove_folder(const model::command::command& cmd);
    void do_save_label(const model::command::command& cmd);
    void do_remove_label(const model::command::command& cmd);
    void do_attach_label(const model::command::command& cmd);
    void do_detach_label(const model::command::command& cmd);
    void do_save_shortcut(const model::command::command& cmd);
    void do_remove_shortcut(const model::command::command& cmd);
    void do_connect_session(const model::command::command& cmd);
    void do_store_credential(const model::command::command& cmd);

    /**
     * @brief Seals a password and sends it back to be stored.
     *
     * The key derivation is deliberate work, so it runs on the pool and the
     * sealed value returns as a command the frame loop applies. Both secrets
     * are wiped when the coroutine ends.
     */
    void start_credential_store(std::string account_id, std::string password,
        model::domain::secret_string master_password);
    void do_disconnect_session(const model::command::command& cmd);
    void do_set_master_password(const model::command::command& cmd);
    void do_unlock_store(const model::command::command& cmd);
    void do_lock_store();
    void do_open_window(const model::command::command& cmd);
    void do_close_window(const model::command::command& cmd);
    void do_refresh_window(const model::command::command& cmd);

    void do_configure_crm(const model::command::command& cmd);

    void do_load_crm_names(const model::command::command& cmd);

    /**
     * @brief Asks for one command centre's deployment read.
     *
     * One read at a time per session. A read already in flight means the
     * cadence asked again before the deployment answered, and a second read
     * would queue behind the first for no new information.
     */
    void start_deployment_read(std::string_view window_id);

    /**
     * @brief Binds every restored window saved against one session pair.
     *
     * A window wall is restored before any session exists, so its windows are
     * unbound. The sign-in that creates the pair is the moment they can be
     * bound, and the refresh deadlines move to now so each reads its data
     * immediately rather than waiting a cadence.
     */
    void rebind_restored_windows(std::string_view instance_id,
        std::string_view account_id, model::state::session_handle session);

    /** @brief Asks for the market series catalogue, on the window's session. */
    void start_market_catalogue_read(std::string_view window_id);

    /**
     * @brief Asks for one market read on the window's session.
     *
     * An empty @p series_id is the catalogue; anything else is that series'
     * points. One path because the connection, token and in-flight rules are
     * the same for both.
     */
    void start_market_read(std::string_view window_id,
        std::string_view series_id);

    /** @brief Asks for one series' points, on the window's session. */
    void start_market_points_read(std::string_view window_id,
        std::string_view series_id);

    /** @brief Reads a tenant's currency-to-image map. */
    void start_currency_image_map(const model::event::event& ev);

    /** @brief Reads one tenant image by identifier for a session. */
    void do_load_image(const model::command::command& cmd);

    /** @brief Reads the latest curve named by a set of tenor series. */
    void do_load_term_structure(const model::command::command& cmd);

    /** @brief Reads the curve a term-structure window shows. */
    void start_term_structure_read(model::state::window_handle handle);

    /** @brief Reads the CRM batch a cross-rates window shows. */
    void start_crm_read(model::state::window_handle handle);

    /**
     * @brief Reads each CRM batch an open cross-rates window shows, when due.
     *
     * The deadline belongs to the batch rather than the window, so any number
     * of windows over one matrix cost one read per cadence. Whether a window is
     * collapsed or drawn plays no part, because the interface never asks.
     */
    void pump_crm_reads();

    void start_crm_names_read(std::string_view instance_id,
        std::string_view account_id, std::string_view party_id);

    /** @brief Seeds one watched series' history on a fresh connection. */
    void start_market_backfill(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id);

    /** @brief Opens the live stream for one authenticated session. */
    void start_market_stream(const model::event::event& ev);

    /** @brief Stops and forgets one session's live stream. */
    void stop_market_stream(std::string_view instance_id,
        std::string_view account_id);

    /** @brief Asks one stream to stop without waiting for it. */
    void retire_market_stream(std::unique_ptr<worker::market_stream> stream);

    /** @brief Reconciles watches, drains buckets and applies them to the model. */
    void pump_market_streams();
    /**
     * @brief Drops every shortcut pointing at a target that is going away.
     *
     * Called from the two removals whose rows a shortcut can name, so the bar
     * never offers an entry that opens something the store no longer holds. The
     * store call is made through @c store_failure like every other write, so a
     * refusal reaches the status bar rather than leaving the entry behind.
     */
    void drop_target_shortcuts(model::domain::window_kind kind,
                               const std::string& target_id);

    /**
     * @brief Clears a read's in-flight mark and applies its answer.
     *
     * @return true when the event was wholly a read answer, false when the
     * session row still has something to apply.
     */
    [[nodiscard]] bool apply_read_answer(const model::event::event& ev);

    /**
     * @brief Moves every leaf out of @p folder_ids and into its tree's root.
     *
     * Called before a folder removal, so that removing a folder removes the
     * folder rather than the operator's instances and accounts. A leaf whose
     * tree has no root -- which a store written before roots existed could
     * hold -- is left where it is rather than moved to nowhere, and the
     * runtime reports that the tree is broken instead of silently detaching it.
     */
    void rehome_leaves(const std::vector<std::string>& folder_ids);

    void ensure_instance_manager_window();

    /**
     * @brief Hands one connect attempt to the session pool.
     *
     * The tail of @c do_connect_session, split out because a connect whose
     * credential is stored reaches it from a coroutine instead: the sealed
     * secret can only be opened off the frame loop, so the attempt is started
     * after that work lands rather than before.
     *
     * @p sealed_credential is empty for a password typed at the prompt and
     * holds the stored blob when there is one. Exactly one of the two carries
     * the secret, and the worker refuses to treat them as alternatives.
     */
    void start_session_for(const model::command::command& cmd,
                           model::domain::credential credential,
                           std::string sealed_credential = {});

    /**
     * @brief Opens the account's stored credential off the frame loop.
     *
     * Derives the key on the pool and, when it opens, starts the session from
     * there. Every failure comes back as an event, so the frame loop never
     * waits on the derivation and never sees the plaintext.
     */
    void start_stored_connect(const model::command::command& cmd,
                              std::string sealed);

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

    /**
     * @brief Forgets the held master password and returns to @c locked.
     *
     * @p deliberate is the operator having asked for it, which leaves the store
     * @c locked; anything else is a check that failed, which leaves it
     * @c failed. The distinction is the whole reason the two states exist.
     */
    void drop_master_password(bool deliberate);

    void stop_session(const std::string& window_or_session_id);

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

    model::state::instance_table instances_;
    model::state::account_table accounts_;
    model::state::folder_table folders_;
    model::state::label_table labels_;
    model::state::labelling_table labellings_;
    model::state::shortcut_table shortcuts_;
    model::state::session_table sessions_;
    model::state::window_table windows_;
    model::command::command_queue commands_;
    model::command::event_queue events_;

    std::unique_ptr<store::sqlite::database> database_;
    std::unique_ptr<store::repository::instance_repository> instance_repo_;
    std::unique_ptr<store::repository::account_repository> account_repo_;
    std::unique_ptr<store::repository::folder_repository> folder_repo_;
    std::unique_ptr<store::repository::label_repository> label_repo_;
    std::unique_ptr<store::repository::labelling_repository> labelling_repo_;
    std::unique_ptr<store::repository::shortcut_repository> shortcut_repo_;
    std::unique_ptr<store::repository::window_repository> window_repo_;
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

    /**
     * @brief The password a pool thread has verified and is handing over.
     *
     * Written by the unlock coroutine and picked up by @c apply on the frame
     * loop, and never both at once: only one unlock runs at a time and the
     * frame loop is the only reader. It exists because @c master_password_
     * itself may not be assigned from a pool thread -- the frame loop reads it
     * on every connect -- so the handover needs a slot that is written once,
     * before the event that announces it, and read once, after.
     */
    model::domain::secret_string pending_master_password_;

    model::domain::store_lock store_lock_ = model::domain::store_lock::absent;

    /** @brief Whether a derivation is running on the pool right now. */
    bool unlocking_ = false;

    std::unordered_map<std::string, std::shared_ptr<std::stop_source>> running_;

    /**
     * @brief The deployment reads in flight, by the session they serve.
     *
     * A read is short-lived and has no cancellation of its own, so a set of
     * keys is all the state it needs: an entry means one read is out, and the
     * snapshot event removes it whatever the read found.
     */
    std::unordered_set<std::string> deployment_reads_;

    /** @brief The market series and the points each chart holds. */
    model::state::market_table market_;

    /** @brief Which image each tenant currency names. */
    model::state::tenant_image_map currency_images_;

    /** @brief Tenants whose currency image map read is out. */
    std::unordered_set<std::string> currency_images_reading_;

    /** @brief The bounded cache of tenant images by identifier. */
    model::state::image_cache images_;

    /** @brief The latest curve each browser session read. */
    model::state::term_structure_table term_structures_;

    model::state::crm_table crm_;

    /** @brief Tenant-and-image whose read is out. */
    std::unordered_set<std::string> images_reading_;

    /** @brief Tenant-and-image whose read failed, so it is not asked again. */
    std::unordered_set<std::string> failed_images_;

    /** @brief The sessions whose catalogue read is out, by running key. */
    std::unordered_set<std::string> catalogues_reading_;

    /** @brief The session-and-series whose points read is out. */
    std::unordered_set<std::string> points_reading_;

    /**
     * @brief One authenticated session's live prices.
     *
     * The stream owns the connection and its thread; the manager owns the
     * watch set, the backfill and the drain into the model. Keyed by the
     * session's running key.
     */
    struct market_stream_entry final {
        std::string instance_id;
        std::string account_id;
        std::unique_ptr<worker::market_stream> stream;
        std::unordered_map<std::string, std::string> watched;
    };

    std::unordered_map<std::string, market_stream_entry> market_streams_;

    /**
     * @brief Streams asked to stop, joined once each says it has finished.
     *
     * A stream's thread can be inside a connect, so the frame thread never
     * joins one that is still running. The pump joins the finished ones,
     * which costs nothing, and shutdown joins the rest.
     */
    std::vector<std::unique_ptr<worker::market_stream>> retired_market_streams_;

    /**
     * @brief The attempt each pair is on, by the pair's running key.
     *
     * A replacement worker is started while the old one is still winding down.
     * Every fact carries the attempt that produced it, and a fact from an
     * older attempt is dropped, so the old worker's disconnect cannot overwrite
     * the new worker's sign-in.
     */
    std::unordered_map<std::string, std::uint64_t> attempts_;
    std::uint64_t next_attempt_ = 0;

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

    /** @brief Why the last unlock was refused. See @c lock_error(). */
    std::string lock_error_;
    std::uint64_t next_window_serial_ = 1;
    bool started_ = false;

    /*
     * Last, so both are built after and destroyed before the tables, the
     * channel and the options they hold references to.
     */
    service::probe_service probe_service_{instances_, probe_pool_.context(), events_};
    service::password_policy_service policy_service_{seed_options_,
        probe_pool_.context(), events_};
};

}

#endif
