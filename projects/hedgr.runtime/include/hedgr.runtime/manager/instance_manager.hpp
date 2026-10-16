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
#include "hedgr.model/state/password_policy_state.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/export.hpp"
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
        return probes_;
    }

    [[nodiscard]] const model::state::window_table& windows() const noexcept {
        return windows_;
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
        return password_policy_;
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

    /**
     * @brief Why the store could not be brought up, or empty.
     *
     * Kept apart from @c last_error, which every command that fails writes and
     * which a successful unlock clears: that one is the runtime's running note
     * about whatever it last did, and this one is the reason this process has
     * no store at all. Both are worth reading, and only this one decides
     * whether the interface draws the startup notice, so a command that fails
     * later cannot be shown as a store that never opened.
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
    void do_disconnect_session(const model::command::command& cmd);
    void do_set_master_password(const model::command::command& cmd);
    void do_unlock_store(const model::command::command& cmd);
    void do_lock_store();
    void do_open_window(const model::command::command& cmd);
    void do_close_window(const model::command::command& cmd);
    void do_refresh_window(const model::command::command& cmd);
    void do_probe_instance(const model::command::command& cmd);
    void do_probe_all();

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
     * @brief Spawns a probe for every instance whose deadline has passed.
     *
     * Run at the end of pump(), after the commands and the events have been
     * applied, so a probe asked for this frame or an answer that landed this
     * frame is already reflected in the deadline it reads. An instance with a
     * probe in flight is skipped: an instance is never probed twice at once,
     * because two probes racing on one instance would report two results for
     * one moment and would spend two of the pool's two threads doing it.
     */
    void pump_probes();

    /**
     * @brief Asks the deployment the process was pointed at for its policy.
     *
     * Run once at start. Not from the create prompt's own frame and not per
     * frame: the read blocks on the transport, so the interface asks for it by
     * starting the process and then reads whatever came back. Asking once is
     * also what keeps a refused read from becoming a poll loop against a
     * deployment that is already not answering.
     */
    void fetch_password_policy();

    /** @brief Applies one policy read's answer. */
    void apply_password_policy(const model::event::event& ev);

    /** @brief Starts one probe, and remembers that it is in flight. */
    void spawn_probe_for(const model::domain::instance& row);

    /** @brief Applies one probe's answer: the result, then the next deadline. */
    void apply_probe_result(const model::event::event& ev);

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

    /**
     * @brief Drops everything the schedule holds for one instance.
     *
     * Called when an instance is removed, so no deadline, no interval and no
     * running probe outlives the row they describe.
     */
    void forget_probe(model::state::instance_handle handle,
                      const std::string& instance_id);

    void seed_first_run();

    /**
     * @brief Gives a seeded instance back the illustration key a migration
     * dropped.
     *
     * The version 7 to 8 migration added @c icon_key empty on every existing
     * row, because empty is the stored meaning of no illustration chosen and
     * the migration would not guess which rows had one. It runs here, where the
     * store has just been read, rather than in the migration, so it reaches
     * every store rather than only the one that happens to migrate next, and it
     * is idempotent because a row whose key is already set is left alone.
     *
     * It only writes a row whose id carries the seeder's prefix and whose
     * derived key has an asset behind it, so a hand-typed row is never given a
     * picture. A refused write is reported through @c last_error_.
     */
    void backfill_seeded_icon_keys();

    /**
     * @brief Makes sure the folder the seeded environments sit in exists.
     *
     * @return The folder's id, which is a constant rather than a generated key:
     * the seeder runs once per store, and the folder is a fact about the
     * environments it seeds rather than a row an operator named.
     */
    [[nodiscard]] std::string ensure_development_folder();

    /**
     * @brief Makes sure the label the seeded environments carry exists.
     *
     * @return The label's id, which is the id of an existing label of the same
     * name when the operator already has one: a label's name is unique, so a
     * second row carrying that name would be refused and the seeded instances
     * would come up unlabelled.
     */
    [[nodiscard]] std::string ensure_development_label();

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

    /**
     * @brief Creates the folders a tree is anchored by, when they are missing.
     *
     * Idempotent, and run on every start: an instance added by a build that did
     * not create accounts roots still gets one, which is what stops a start
     * from depending on the order the store was written in.
     */
    void ensure_roots();

    /** @brief Creates the accounts root of one instance, if it has none. */
    void ensure_accounts_root(const std::string& instance_id);

    /**
     * @brief Makes sure the folder the seeded logins sit in exists.
     *
     * @return The folder's id, which is derived from the instance id rather
     * than generated. The seeder runs on every start, and the derived key is
     * what makes a second pass write the same folder rather than a second one
     * beside it.
     */
    [[nodiscard]] std::string ensure_privileged_folder(
        const std::string& instance_id);

    /**
     * @brief Gives every stored instance the two logins it is seeded with.
     *
     * Run on every start rather than only on the first, because the walk has to
     * reach an instance this build never seeded as well as one it just made,
     * and because a row that is already there is replaced under the same
     * derived key rather than added to.
     */
    void seed_instance_accounts();

    /**
     * @brief Writes the two logins of one instance, a super admin and a tenant
     * admin.
     *
     * The principals are built from the instance's subject prefix, so they name
     * the deployment IAM is being asked about; the rows land in that instance's
     * accounts root.
     */
    void seed_accounts_for_instance(const model::domain::instance& instance);

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
    model::state::probe_table probes_;
    model::state::password_policy_state password_policy_;
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
     * @brief The probes that are in flight, by the instance they measure.
     *
     * An entry is what stops an instance being probed twice at once, and the
     * stop source beside it is what cancels a probe whose instance has gone or
     * whose application is stopping.
     */
    std::unordered_map<std::string, std::shared_ptr<std::stop_source>> probing_;

    /**
     * @brief The policy read, once it has been asked for.
     *
     * Held apart from the sessions' cancellations because it is not one: it
     * belongs to the deployment rather than to a session or an instance, and a
     * key is not something it has. It exists so that a stop cancels a read
     * still waiting on a connect, rather than leaving a pool thread holding a
     * socket for a process that is going away.
     */
    std::shared_ptr<std::stop_source> policy_read_;

    /**
     * @brief The cadence each instance currently polls on, by instance id.
     *
     * The deadline itself lives in the probe table, beside the result; this is
     * the other half of the same schedule, because doubling a backoff needs to
     * know what the previous one was. An instance with no entry is on the
     * default cadence, which is the state every instance starts in.
     */
    std::unordered_map<std::string, std::chrono::milliseconds> probe_intervals_;

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

    std::string last_error_;

    /** @brief Why the store could not be brought up. See @c startup_failure(). */
    std::string startup_failure_;

    /** @brief Why the last unlock was refused. See @c lock_error(). */
    std::string lock_error_;
    std::uint64_t next_window_serial_ = 1;
    bool started_ = false;

    /**
     * @brief Whether the policy read has been asked for.
     *
     * The read starts on the first pump rather than in @c start, because the
     * answer arrives as an event and the frame loop is the only thing that
     * applies one.
     */
    bool policy_requested_ = false;
};

}

#endif
