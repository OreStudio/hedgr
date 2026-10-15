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
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/worker/probe_worker.hpp"
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::runtime::manager {

namespace {

namespace command = hedgr::model::command;
namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace model_state = hedgr::model::state;
namespace sqlite = hedgr::store::sqlite;
namespace store_crypto = hedgr::store::crypto;
namespace transport = hedgr::session::transport;

/**
 * @brief The key a running worker is registered under.
 *
 * The id separator is the one the session table uses internally to join a pair
 * as well, so a key here can never collide with either half of another pair:
 * "a" + "bc" and "ab" + "c" have to be two workers, not one.
 */
[[nodiscard]] std::string running_key(std::string_view instance_id,
    std::string_view account_id) {
    std::string result;
    result.reserve(instance_id.size() + account_id.size() + 1);
    result.append(instance_id);
    result.push_back('\x1f');
    result.append(account_id);
    return result;
}

/** @brief Every live session row, holes skipped, for a whole-table walk. */
[[nodiscard]] std::vector<model_state::session_handle>
live_sessions(const model_state::session_table& sessions) {
    std::vector<model_state::session_handle> result;
    const auto& layout = sessions.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        result.push_back(model_state::session_handle{index, layout.generation[index]});
    }
    return result;
}

/** @brief Every live window row, holes skipped, for a whole-table walk. */
[[nodiscard]] std::vector<model_state::window_handle>
live_windows(const model_state::window_table& windows) {
    std::vector<model_state::window_handle> result;
    const auto& layout = windows.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        result.push_back(model_state::window_handle{index, layout.generation[index]});
    }
    return result;
}

/**
 * @brief Every live account row, holes skipped, for a whole-table walk.
 *
 * Removing an instance cascades to its accounts, and the rows to drop are found
 * by walking the slab rather than by a lookup, because the question asked is
 * "whose instance is this" rather than "which row is this id".
 */
[[nodiscard]] std::vector<model_state::account_handle>
live_accounts(const model_state::account_table& accounts) {
    std::vector<model_state::account_handle> result;
    const auto& layout = accounts.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        result.push_back(model_state::account_handle{index, layout.generation[index]});
    }
    return result;
}

/** @brief The live slot index of every row of a table, holes skipped. */
[[nodiscard]] std::vector<std::uint32_t>
live_indices(const model_state::slab& layout) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        result.push_back(index);
    }
    return result;
}

/**
 * @brief The current UTC instant as ISO-8601, for a creation timestamp.
 *
 * Formatted by hand rather than through std::format's chrono support, which
 * still varies by standard library: a timestamp column that writes nothing on
 * one toolchain and a date on another is worse than four lines here.
 */
[[nodiscard]] std::string iso8601_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

/** @brief A store failure as one sentence naming the operation that failed. */
[[nodiscard]] std::string store_failure(std::string_view what,
    const sqlite::store_error& error) {
    return std::format("{}: {} ({})", what, error.message,
        sqlite::to_string(error.code));
}

/** @brief The monotonic clock in nanoseconds, which is what deadlines use. */
[[nodiscard]] std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * @brief How often an instance is probed when the last probe answered.
 *
 * Ten seconds is a glance rather than a gate: often enough that a status is
 * never stale when somebody looks at it, rare enough that polling every
 * instance costs the deployment nothing worth measuring.
 */
constexpr std::chrono::milliseconds probe_cadence{10000};

/**
 * @brief The longest an unreachable instance waits between attempts.
 *
 * A deployment that is down is not brought back by being asked faster, and a
 * fixed retry against twenty unreachable instances is the retry storm the
 * backoff exists to prevent.
 */
constexpr std::chrono::milliseconds probe_cadence_max{60000};

/**
 * @brief How many probes may be in flight at once.
 *
 * Two, from ADR 5: a slow probe cannot starve the other probes, and at most
 * two connects are in flight at once, which is a bound an operator can reason
 * about when twenty instances are unreachable.
 */
constexpr std::size_t probe_thread_count = 2;

/** @brief The default kind-specific state for a window opened by kind. */
[[nodiscard]] domain::window_state
default_state_for(domain::window_kind kind, std::string_view last_error) {
    switch (kind) {
    case domain::window_kind::chart: {
        domain::chart_state chart;
        chart.label = "Series";
        return chart;
    }
    case domain::window_kind::grid: {
        domain::grid_state grid;
        grid.resource = "trades";
        return grid;
    }
    case domain::window_kind::notice:
    case domain::window_kind::instance_manager: {
        /*
         * The session table keeps the friendly sentence and nothing else, so a
         * notice opened after a failure shows the sentence and leaves the
         * dimmed low-level line empty rather than inventing one.
         */
        domain::notice_state notice;
        notice.title = "Notice";
        if (!last_error.empty()) {
            notice.title = "Session error";
            notice.text = std::string(last_error);
        }
        return notice;
    }
    }
    return domain::notice_state{};
}

}

instance_manager::instance_manager(std::size_t pool_threads)
    : pool_(pool_threads), probe_pool_(probe_thread_count) {
}

instance_manager::~instance_manager() {
    stop();
}

void instance_manager::stop() noexcept {
    for (auto& [key, source] : running_) {
        if (source)
            source->request_stop();
    }
    running_.clear();

    /*
     * The probes are cancelled before the pools are stopped, so a probe
     * waiting on a connect is told the application is going away and does not
     * report an outcome nobody is left to read.
     */
    for (auto& [instance_id, source] : probing_) {
        if (source)
            source->request_stop();
    }
    probing_.clear();

    /*
     * The pool is stopped here and not in do_shutdown: pump() may still be
     * called after a shutdown command, because the frame loop decides when the
     * last frame is, and a pump over a stopped context is harmless while a
     * joined one is not.
     */
    pool_.stop();
    probe_pool_.stop();
    started_ = false;
}

std::expected<void, std::string>
instance_manager::start(const std::filesystem::path& database_path,
    const transport::session_options& seed) {
    /*
     * Starting twice converges rather than reloading: the second call would
     * re-read a store this manager already holds open, and the seed below only
     * makes sense against a store it has just read.
     */
    if (started_)
        return {};

    auto opened = sqlite::database::open(database_path);
    if (!opened) {
        last_error_ = store_failure(
            std::format("cannot open the store at {}", database_path.string()),
            opened.error());
        return std::unexpected(last_error_);
    }
    database_ = std::make_unique<sqlite::database>(std::move(opened).value());

    if (auto applied = database_->apply_schema(); !applied) {
        last_error_ = store_failure("cannot bring the store up to date",
            applied.error());
        return std::unexpected(last_error_);
    }

    instance_repo_ = std::make_unique<store::repository::instance_repository>(
        *database_);
    account_repo_ = std::make_unique<store::repository::account_repository>(
        *database_);
    folder_repo_ = std::make_unique<store::repository::folder_repository>(
        *database_);
    label_repo_ = std::make_unique<store::repository::label_repository>(
        *database_);
    labelling_repo_ = std::make_unique<store::repository::labelling_repository>(
        *database_);
    window_repo_ = std::make_unique<store::repository::window_repository>(
        *database_);
    master_repo_ = std::make_unique<store::repository::master_repository>(
        *database_);

    /*
     * The lock is read before anything else, because it says whether the rows
     * below can be trusted to mean anything: the whole catalogue sits behind
     * the master password, so a store with a master row that is not held is a
     * store whose contents are ciphertext. @c absent means a first launch, and
     * the interface offers to create one; @c locked means the operator has been
     * here before and is asked for the password. Neither is a failure, and
     * neither stops the start.
     */
    auto has_master = master_repo_->has_master();
    if (!has_master) {
        last_error_ = store_failure("cannot read the store's master password",
            has_master.error());
        return std::unexpected(last_error_);
    }
    store_lock_ = has_master.value() ? model::domain::store_lock::locked
                                     : model::domain::store_lock::absent;

    /*
     * The seed carries what is the same for every instance -- the TLS material
     * and the timeouts -- because those are process-wide settings rather than
     * per-instance columns. It is kept, not just read, because a session opened
     * an hour from now must be built from the same material.
     */
    seed_options_ = seed;

    auto instances = instance_repo_->list();
    if (!instances) {
        last_error_ = store_failure("cannot read the stored instances",
            instances.error());
        return std::unexpected(last_error_);
    }
    instances_.clear();
    for (const auto& row : instances.value())
        static_cast<void>(instances_.upsert(row));

    auto accounts = account_repo_->list();
    if (!accounts) {
        last_error_ = store_failure("cannot read the stored accounts",
            accounts.error());
        return std::unexpected(last_error_);
    }
    accounts_.clear();
    for (const auto& row : accounts.value())
        static_cast<void>(accounts_.upsert(row));

    auto folders = folder_repo_->list();
    if (!folders) {
        last_error_ = store_failure("cannot read the stored folders",
            folders.error());
        return std::unexpected(last_error_);
    }
    folders_.clear();
    for (const auto& row : folders.value())
        static_cast<void>(folders_.upsert(row));

    auto labels = label_repo_->list();
    if (!labels) {
        last_error_ = store_failure("cannot read the stored labels",
            labels.error());
        return std::unexpected(last_error_);
    }
    labels_.clear();
    for (const auto& row : labels.value())
        static_cast<void>(labels_.upsert(row));

    auto labellings = labelling_repo_->list();
    if (!labellings) {
        last_error_ = store_failure("cannot read the stored label attachments",
            labellings.error());
        return std::unexpected(last_error_);
    }
    labellings_.clear();
    for (const auto& row : labellings.value())
        labellings_.add(row.owner, row.owner_id, row.label_id);

    auto windows = window_repo_->list();
    if (!windows) {
        last_error_ = store_failure("cannot read the stored windows",
            windows.error());
        return std::unexpected(last_error_);
    }
    windows_.clear();
    for (const auto& spec : windows.value()) {
        const auto handle = windows_.insert(spec);

        /*
         * Sessions are not persisted, so on a cold start the bind below finds
         * nothing and the window comes back unbound. That is correct rather
         * than a gap: a session is a live connection, and a window that
         * outlives one renders the session-loss state.
         */
        if (!spec.session)
            continue;
        if (const auto session = sessions_.find(spec.session->instance_id,
                spec.session->account_id)) {
            windows_.bind_session(handle, *session);
        }
    }

    seed_first_run();
    ensure_roots();
    ensure_instance_manager_window();

    /*
     * A seed the store refused is a start failure like any other: the next run
     * would not find the instance and would seed it again, so reporting it now
     * is what stops the interface from showing an instance that no store has.
     */
    if (!last_error_.empty())
        return std::unexpected(last_error_);

    started_ = true;
    return {};
}

void instance_manager::seed_first_run() {
    if (instances_.size() != 0)
        return;

    /*
     * The id is derived from the clock rather than random: this runs once, on a
     * store with no instances in it, and a timestamp says plainly where the row
     * came from. A uuid would imply a distributed uniqueness requirement that
     * does not exist here.
     */
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                           .count();

    domain::instance row;
    row.id = std::format("inst-{}", stamp);
    row.name = "local development";
    row.nats_url = seed_options_.nats_url;
    row.subject_prefix = seed_options_.subject_prefix;
    row.wire_format = seed_options_.wire_format;
    row.created_at = iso8601_now();

    static_cast<void>(instances_.upsert(row));

    if (auto saved = instance_repo_->save(row); !saved) {
        last_error_ = store_failure(
            std::format("cannot save the seeded instance {}", row.id), saved.error());
        return;
    }

    /*
     * The seed is the reason a fresh checkout is usable without typing an
     * address: the caller's configuration describes the local deployment, and this is
     * where that description becomes a row.
     */
}

void instance_manager::ensure_roots() {
    /*
     * The instances tree is anchored once and never again. Its root has no
     * owner, because there is exactly one such tree and nothing distinguishes
     * it from another; the partial unique index in the schema is what holds
     * that to one row.
     */
    if (!folders_.find(domain::instances_root_id)) {
        domain::folder root;
        root.id = std::string{domain::instances_root_id};
        root.parent_id.clear();
        root.owner_id.clear();
        root.name = "Instances";
        root.tree = domain::folder_tree::instances;
        static_cast<void>(folders_.upsert(root));

        if (folder_repo_) {
            if (auto saved = folder_repo_->save(root); !saved) {
                last_error_ = store_failure(
                    std::format("cannot save the instances root {}", root.id),
                    saved.error());
            }
        }
    }

    /*
     * Every instance owns an accounts tree, so this runs over the instances
     * rather than over the folders: an instance stored by a build that did not
     * create roots gets its own here, which is what makes the step idempotent
     * across a version change as well as across a restart.
     */
    for (const auto index : live_indices(instances_.layout())) {
        const auto& row = instances_.rows()[index];
        ensure_accounts_root(row.id);
    }
}

void instance_manager::ensure_accounts_root(const std::string& instance_id) {
    if (instance_id.empty())
        return;

    const auto id = domain::accounts_root_id_for(instance_id);
    if (folders_.find(id))
        return;

    domain::folder root;
    root.id = id;
    root.parent_id.clear();
    root.owner_id = instance_id;
    root.name = "Accounts";
    root.tree = domain::folder_tree::accounts;
    static_cast<void>(folders_.upsert(root));

    if (!folder_repo_)
        return;
    if (auto saved = folder_repo_->save(root); !saved) {
        last_error_ = store_failure(
            std::format("cannot save the accounts root {}", id), saved.error());
    }
}

void instance_manager::ensure_instance_manager_window() {
    if (windows_.find("instance-manager"))
        return;

    domain::window_spec spec;
    spec.id = "instance-manager";
    spec.kind = domain::window_kind::instance_manager;

    /*
     * No session, and never one: the Instance Manager is the root of the
     * interface and is the surface a person uses to open a session in the first
     * place.
     */
    spec.session = std::nullopt;

    /* The handle is discarded on purpose: nothing binds to this window and its
     * stable id is how everything else finds it. */
    static_cast<void>(windows_.insert(spec));
}

void instance_manager::pump() {
    /*
     * Draining into a local and passing each command by mutable reference, so
     * that @c handle can wipe the password the command carried once it has been
     * used. The alternative leaves a secret in the vector's buffer until it is
     * overwritten by an unrelated allocation, which is the same defect @c
     * secret_string exists to prevent everywhere else.
     */
    for (auto& request : commands_.drain())
        handle(request);
    for (const auto& fact : events_.drain())
        apply(fact);

    /*
     * Last, so a probe asked for by a command this frame starts now rather
     * than on the next one, and so an answer applied above has already moved
     * the deadline the schedule reads.
     */
    pump_probes();
}

void instance_manager::handle(command::command& cmd) {
    switch (cmd.kind) {
    case command::command_kind::save_instance:
        do_save_instance(cmd);
        break;
    case command::command_kind::remove_instance:
        do_remove_instance(cmd);
        break;
    case command::command_kind::save_account:
        do_save_account(cmd);
        break;
    case command::command_kind::remove_account:
        do_remove_account(cmd);
        break;
    case command::command_kind::save_folder:
        do_save_folder(cmd);
        break;
    case command::command_kind::remove_folder:
        do_remove_folder(cmd);
        break;
    case command::command_kind::save_label:
        do_save_label(cmd);
        break;
    case command::command_kind::remove_label:
        do_remove_label(cmd);
        break;
    case command::command_kind::attach_label:
        do_attach_label(cmd);
        break;
    case command::command_kind::detach_label:
        do_detach_label(cmd);
        break;
    case command::command_kind::connect_session:
        do_connect_session(cmd);
        break;
    case command::command_kind::disconnect_session:
        do_disconnect_session(cmd);
        break;
    case command::command_kind::open_window:
        do_open_window(cmd);
        break;
    case command::command_kind::close_window:
        do_close_window(cmd);
        break;
    case command::command_kind::refresh_window:
        do_refresh_window(cmd);
        break;
    case command::command_kind::probe_instance:
        do_probe_instance(cmd);
        break;
    case command::command_kind::probe_all:
        do_probe_all();
        break;
    case command::command_kind::set_master_password:
        do_set_master_password(cmd);
        break;
    case command::command_kind::unlock_store:
        do_unlock_store(cmd);
        break;
    case command::command_kind::lock_store:
        do_lock_store();
        break;
    case command::command_kind::shutdown:
        /*
         * Every worker is asked to stop and the manager stops being started,
         * but the pool stays up: pump() may still be called, and the frame loop
         * owns the decision about when there are no more frames.
         */
        for (auto& [key, source] : running_) {
            if (source)
                source->request_stop();
        }
        running_.clear();

        /*
         * A probe is cancelled here too, and the schedule is left alone: the
         * probe table is runtime state that dies with the process rather than
         * something a shutdown has to unwind.
         */
        for (auto& [instance_id, source] : probing_) {
            if (source)
                source->request_stop();
        }
        probing_.clear();
        started_ = false;
        break;
    }

    /*
     * The password is wiped once the command has been acted on, on every path
     * including the ones that refused it. Nothing below this point needs it --
     * a credential is handed to a worker during the dispatch above, not after
     * it -- so the only thing a surviving copy could do is be read by whoever
     * gets that allocation next.
     *
     * The same three steps the session worker uses, for the same reason: clear()
     * alone leaves the bytes in the allocation.
     */
    std::fill(cmd.password.begin(), cmd.password.end(), '\0');
    cmd.password.clear();
    cmd.password.shrink_to_fit();
}

void instance_manager::do_save_instance(const command::command& cmd) {
    if (!instance_repo_) {
        last_error_ = "cannot save an instance: the store is not open";
        return;
    }

    /*
     * The row is remembered before it is replaced so that a store failure can
     * put the table back the way it was. A table that shows an instance the
     * store never accepted would be a lie the next start would contradict.
     */
    const auto existing = instances_.find(cmd.instance_row.id);
    const auto previous = existing
        ? std::optional<domain::instance>(*instances_.get(*existing))
        : std::nullopt;

    const auto handle = instances_.upsert(cmd.instance_row);

    if (auto saved = instance_repo_->save(cmd.instance_row); !saved) {
        if (previous)
            static_cast<void>(instances_.upsert(*previous));
        else
            instances_.remove(handle);
        last_error_ = store_failure(
            std::format("cannot save instance {}", cmd.instance_row.id),
            saved.error());
        return;
    }

    /*
     * The instance is stored, so it needs somewhere for its accounts to live.
     * The root is created here rather than lazily on the first account, because
     * a tree with no root is a shape the Browser cannot draw.
     */
    ensure_accounts_root(cmd.instance_row.id);
}

void instance_manager::do_remove_instance(const command::command& cmd) {
    /*
     * The interface names the row by id and the row itself also carries one, so
     * either is accepted rather than dropping a removal that named the
     * instance in the field the other caller used.
     */
    const std::string id = cmd.instance_id.empty() ? cmd.instance_row.id
                                                   : cmd.instance_id;
    if (id.empty()) {
        last_error_ = "cannot remove an instance: no instance was named";
        return;
    }

    if (const auto handle = instances_.find(id)) {
        /*
         * Everything the schedule holds for this instance goes with it, and
         * the probe already in flight is cancelled: a result for a removed
         * instance would be applied to whatever takes its slot next, and the
         * deadline would keep a probe running for a row nobody can read.
         */
        forget_probe(*handle, id);
        instances_.remove(*handle);
    }

    /*
     * The cascade is a walk rather than a lookup: what the accounts table is
     * asked is which rows name this instance, and its handles are what a
     * removal takes.
     */
    for (const auto account : live_accounts(accounts_)) {
        if (accounts_.get(account)->instance_id == id)
            accounts_.remove(account);
    }

    /*
     * The instance's own folders go with it: every root of an accounts tree
     * names the instance as its owner, so the tree is found by the owner rather
     * than by walking the folders.
     */
    {
        const auto accounts_root = folders_.root(domain::folder_tree::accounts, id);
        const auto doomed = accounts_root
            ? folders_.descendants_of(folders_.get(*accounts_root)->id)
            : std::vector<std::string>{};
        if (accounts_root)
            folders_.remove(*accounts_root);
        for (const auto& folder_id : doomed) {
            if (const auto handle = folders_.find(folder_id))
                folders_.remove(*handle);
        }
    }
    if (folder_repo_) {
        if (auto removed = folder_repo_->remove_tree(
                domain::folder_tree::accounts, id);
            !removed) {
            last_error_ = store_failure(
                std::format("cannot remove the folders of instance {}", id),
                removed.error());
        }
    }

    /* A label attachment is the instance's, so it goes with the instance. */
    labellings_.remove_owner(domain::labelling_owner::instance, id);
    if (labelling_repo_) {
        if (auto removed = labelling_repo_->remove_owner(
                domain::labelling_owner::instance, id);
            !removed) {
            last_error_ = store_failure(
                std::format("cannot remove the labels of instance {}", id),
                removed.error());
        }
    }

    /*
     * A session belongs to the pair, so removing the instance removes its
     * sessions: the worker is asked to stop before the row goes, and any event
     * it still manages to send arrives for a session that is no longer there
     * and is ignored.
     */
    for (const auto session : live_sessions(sessions_)) {
        if (sessions_.instance_id(session) != id)
            continue;

        stop_session(running_key(sessions_.instance_id(session),
            sessions_.account_id(session)));

        /*
         * The window stays open and loses its session. Closing it would take
         * away something the operator put on screen because of a change made
         * somewhere else; a window with no session shows the session-loss state
         * instead. This has to happen before the row is released, because
         * releasing bumps the generation and the handle the window holds would
         * stop comparing equal to the row it points at.
         */
        for (const auto window : live_windows(windows_)) {
            if (windows_.session_of(window) == session)
                windows_.clear_session(window);
        }

        sessions_.remove(session);
    }

    if (instance_repo_) {
        if (auto removed = instance_repo_->remove(id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove instance {}", id), removed.error());
        }
    }
    if (account_repo_) {
        if (auto removed = account_repo_->remove_for_instance(id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove the accounts of instance {}", id),
                removed.error());
        }
    }
    if (auto saved = save_windows(); !saved)
        last_error_ = saved.error();
}

void instance_manager::do_save_account(const command::command& cmd) {
    if (!account_repo_) {
        last_error_ = "cannot save an account: the store is not open";
        return;
    }

    const auto existing = accounts_.find(cmd.account_row.id);
    const auto previous = existing
        ? std::optional<domain::account>(*accounts_.get(*existing))
        : std::nullopt;

    const auto handle = accounts_.upsert(cmd.account_row);

    if (auto saved = account_repo_->save(cmd.account_row); !saved) {
        if (previous)
            static_cast<void>(accounts_.upsert(*previous));
        else
            accounts_.remove(handle);
        last_error_ = store_failure(
            std::format("cannot save account {}", cmd.account_row.id),
            saved.error());
    }
}

void instance_manager::do_remove_account(const command::command& cmd) {
    const std::string id = cmd.account_id.empty() ? cmd.account_row.id
                                                  : cmd.account_id;
    if (id.empty()) {
        last_error_ = "cannot remove an account: no account was named";
        return;
    }

    /*
     * One account is at most one session, because a session is keyed by the
     * pair, so the row to stop is looked up by the pair rather than walked for.
     * The instance is read out before the row goes, because removing it clears
     * the value the slot held.
     */
    const auto handle = accounts_.find(id);
    const std::string instance_id = handle
        ? accounts_.get(*handle)->instance_id
        : cmd.instance_id;

    if (handle)
        accounts_.remove(*handle);

    if (const auto session = sessions_.find(instance_id, id)) {
        stop_session(running_key(instance_id, id));
        for (const auto window : live_windows(windows_)) {
            if (windows_.session_of(window) == *session)
                windows_.clear_session(window);
        }
        sessions_.remove(*session);
    }

    if (account_repo_) {
        if (auto removed = account_repo_->remove(id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove account {}", id), removed.error());
        }
    }
    if (auto saved = save_windows(); !saved)
        last_error_ = saved.error();
}

void instance_manager::do_save_folder(const command::command& cmd) {
    if (!folder_repo_) {
        last_error_ = "cannot save a folder: the store is not open";
        return;
    }

    const auto& incoming = cmd.folder_row;
    if (incoming.id.empty()) {
        last_error_ = "cannot save a folder: no folder was named";
        return;
    }
    if (incoming.id == domain::instances_root_id) {
        last_error_ = std::format(
            "cannot re-parent folder {}: it is the root of the instances tree",
            incoming.id);
        return;
    }
    if (incoming.name.empty()) {
        last_error_ = std::format("cannot save folder {}: it has no name",
            incoming.id);
        return;
    }

    const auto existing = folders_.find(incoming.id);
    const auto previous = existing
        ? std::optional<domain::folder>(*folders_.get(*existing))
        : std::nullopt;

    /*
     * The tree and the owner are a property of where a folder sits, not of the
     * folder, so they come from the parent rather than from the command. Asking
     * the caller to repeat them would be a second place they could be wrong,
     * and getting them wrong would put a folder in a tree it does not belong
     * to.
     */
    domain::folder row = incoming;
    if (const auto parent = folders_.find(incoming.parent_id)) {
        const auto& parent_row = *folders_.get(*parent);
        row.tree = parent_row.tree;
        row.owner_id = parent_row.owner_id;
    } else if (previous) {
        row.tree = previous->tree;
        row.owner_id = previous->owner_id;
    } else {
        last_error_ = std::format(
            "cannot save folder {}: no parent folder {}", incoming.id,
            incoming.parent_id.empty() ? "<none>" : incoming.parent_id);
        return;
    }
    if (row.parent_id.empty()) {
        last_error_ = std::format(
            "cannot save folder {}: only a root has no parent", incoming.id);
        return;
    }

    /*
     * A cycle is refused by the table as well, because a table is reachable
     * without this handler. It is reported here too so that the sentence names
     * the folder the operator asked about rather than the id the table saw.
     */
    if (row.id == row.parent_id) {
        last_error_ = std::format("cannot parent folder {} to itself", row.id);
        return;
    }

    auto handle = folders_.upsert(row);
    if (!handle) {
        last_error_ = handle.error();
        return;
    }

    if (auto saved = folder_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(folders_.upsert(*previous));
        else
            folders_.remove(*handle);
        last_error_ = store_failure(
            std::format("cannot save folder {}", row.id), saved.error());
    }
}

void instance_manager::do_remove_folder(const command::command& cmd) {
    if (!folder_repo_) {
        last_error_ = "cannot remove a folder: the store is not open";
        return;
    }

    const std::string id = cmd.folder_row.id.empty() ? cmd.payload
                                                    : cmd.folder_row.id;
    if (id.empty()) {
        last_error_ = "cannot remove a folder: no folder was named";
        return;
    }

    /*
     * The root of a tree is what the tree is anchored by: removing it would
     * leave every folder below it unreachable, so the refusal is here in the
     * runtime and not only in whatever surfaced the request.
     */
    if (id == domain::instances_root_id) {
        last_error_ = std::format(
            "cannot remove folder {}: it is the root of the instances tree", id);
        return;
    }
    if (const auto handle = folders_.find(id)) {
        if (folders_.get(*handle)->parent_id.empty()) {
            last_error_ = std::format(
                "cannot remove folder {}: it is a root of its tree", id);
            return;
        }
    }

    /*
     * The children are collected before anything goes, because a removal
     * releases the slot and a walk from a released row would find nothing.
     */
    const auto doomed = folders_.descendants_of(id);

    if (const auto handle = folders_.find(id))
        folders_.remove(*handle);
    for (const auto& folder_id : doomed) {
        if (const auto handle = folders_.find(folder_id))
            folders_.remove(*handle);
    }

    /*
     * The store is asked for the same cascade by id rather than left to a
     * foreign key: a folder's parent is a folder, so a self-referencing cascade
     * would need a constraint this schema does not carry, and the removal would
     * then depend on the order the rows were walked in.
     */
    if (auto removed = folder_repo_->remove(id); !removed) {
        last_error_ = store_failure(
            std::format("cannot remove folder {}", id), removed.error());
    }
    for (const auto& folder_id : doomed) {
        if (auto removed = folder_repo_->remove(folder_id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove folder {}", folder_id),
                removed.error());
        }
    }
}

void instance_manager::do_save_label(const command::command& cmd) {
    if (!label_repo_) {
        last_error_ = "cannot save a label: the store is not open";
        return;
    }

    const auto& row = cmd.label_row;
    if (row.id.empty()) {
        last_error_ = "cannot save a label: no label was named";
        return;
    }
    if (row.name.empty()) {
        last_error_ = std::format("cannot save label {}: it has no name",
            row.id);
        return;
    }

    const auto existing = labels_.find(row.id);
    const auto previous = existing
        ? std::optional<domain::label>(*labels_.get(*existing))
        : std::nullopt;

    const auto handle = labels_.upsert(row);

    if (auto saved = label_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(labels_.upsert(*previous));
        else
            labels_.remove(handle);
        last_error_ = store_failure(
            std::format("cannot save label {}", row.id), saved.error());
    }
}

void instance_manager::do_remove_label(const command::command& cmd) {
    if (!label_repo_) {
        last_error_ = "cannot remove a label: the store is not open";
        return;
    }

    const std::string id = cmd.label_row.id.empty() ? cmd.payload
                                                   : cmd.label_row.id;
    if (id.empty()) {
        last_error_ = "cannot remove a label: no label was named";
        return;
    }

    /*
     * A label is removed together with its attachments, in both the table and
     * the store, so no attachment is left naming a label that is gone. The
     * store would cascade on its own; saying it here as well keeps the two
     * agreeing even if a later schema drops the cascade.
     */
    const auto attachments = labellings_.rows();
    if (const auto handle = labels_.find(id))
        labels_.remove(*handle);
    for (const auto& row : attachments) {
        if (row.label_id != id)
            continue;
        labellings_.remove(row.owner, row.owner_id, row.label_id);
        if (auto detached = labelling_repo_->remove(row); !detached) {
            last_error_ = store_failure(
                std::format("cannot detach label {} from {}", id, row.owner_id),
                detached.error());
        }
    }

    if (auto removed = label_repo_->remove(id); !removed) {
        last_error_ = store_failure(
            std::format("cannot remove label {}", id), removed.error());
    }
}

void instance_manager::do_attach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        last_error_ = "cannot attach a label: the store is not open";
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        last_error_ = "cannot attach a label: the owner and the label must be "
                      "named";
        return;
    }

    /*
     * Both ends are resolved before the attachment is written: an attachment to
     * a row that does not exist is a foreign key the store would refuse, and
     * refusing it here says which end was missing.
     */
    if (row.owner == domain::labelling_owner::instance) {
        if (!instances_.find(row.owner_id)) {
            last_error_ = std::format("cannot attach a label: no instance {}",
                row.owner_id);
            return;
        }
    } else if (!accounts_.find(row.owner_id)) {
        last_error_ = std::format("cannot attach a label: no account {}",
            row.owner_id);
        return;
    }
    if (!labels_.find(row.label_id)) {
        last_error_ = std::format("cannot attach a label: no label {}",
            row.label_id);
        return;
    }

    labellings_.add(row.owner, row.owner_id, row.label_id);

    if (auto saved = labelling_repo_->save(row); !saved) {
        labellings_.remove(row.owner, row.owner_id, row.label_id);
        last_error_ = store_failure(
            std::format("cannot attach label {} to {}", row.label_id,
                row.owner_id),
            saved.error());
    }
}

void instance_manager::do_detach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        last_error_ = "cannot detach a label: the store is not open";
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        last_error_ = "cannot detach a label: the owner and the label must be "
                      "named";
        return;
    }

    labellings_.remove(row.owner, row.owner_id, row.label_id);

    if (auto removed = labelling_repo_->remove(row); !removed) {
        last_error_ = store_failure(
            std::format("cannot detach label {} from {}", row.label_id,
                row.owner_id),
            removed.error());
    }
}

void instance_manager::do_connect_session(const command::command& cmd) {
    /*
     * Both ids arrive as text from the interface, so both are resolved here:
     * the instance's row is read for the connection settings, and the account
     * has to exist, because its identity is also what a stored credential is
     * looked up by.
     */
    const auto instance = instances_.find(cmd.instance_id);
    if (!instance) {
        last_error_ = std::format("cannot connect: no instance {}",
            cmd.instance_id);
        return;
    }
    const auto* instance_row = instances_.get(*instance);

    if (!accounts_.find(cmd.account_id)) {
        last_error_ = std::format("cannot connect: no account {}",
            cmd.account_id);
        return;
    }

    /*
     * A credential typed for this attempt wins over a stored one. The operator
     * who has just filled in a password prompt is asking for that password to
     * be used, and quietly preferring the remembered one would make the prompt
     * a lie.
     */
    if (!cmd.password.empty()) {
        start_session_for(cmd, domain::credential{cmd.principal, cmd.password});
        return;
    }

    /*
     * No password and a closed lock is a refusal, and the sentence says which
     * of the two things went wrong. "The password was wrong" would be wrong:
     * nothing was tried, and an operator who is told their password is bad will
     * retype a password that was never the problem.
     */
    if (store_lock_ != domain::store_lock::unlocked) {
        last_error_ = std::format(
            "cannot connect to {}: the store is {}, so no password is "
            "available and none was supplied",
            instance_row->name, domain::to_string(store_lock_));
        return;
    }

    auto sealed = account_repo_->credential_of(cmd.account_id);
    if (!sealed) {
        last_error_ = store_failure("cannot read the stored credential",
            sealed.error());
        return;
    }
    if (!sealed.value()) {
        last_error_ = std::format(
            "cannot connect to {}: the account has no stored credential and "
            "no password was supplied",
            instance_row->name);
        return;
    }

    /*
     * The sealed value goes to the pool to be opened. It is ciphertext and safe
     * to hold here; what comes out of it is not, so it never travels back
     * through this thread.
     */
    auto blob = std::move(sealed).value();
    start_stored_connect(cmd, std::move(blob.value()));
}

void instance_manager::start_session_for(const command::command& cmd,
    domain::credential credential, std::string sealed_credential) {
    const auto instance = instances_.find(cmd.instance_id);
    if (!instance)
        return;
    const auto* instance_row = instances_.get(*instance);

    /*
     * Reconnecting a pair resumes the same session rather than making a second
     * one, which is the whole reason the table is keyed by the pair.
     */
    auto handle = sessions_.find(cmd.instance_id, cmd.account_id);
    if (!handle)
        handle = sessions_.insert(cmd.instance_id, cmd.account_id);
    sessions_.set_status(*handle, domain::session_status::connecting);

    transport::session_options options;
    options.nats_url = instance_row->nats_url;
    options.subject_prefix = instance_row->subject_prefix;
    options.wire_format = instance_row->wire_format;

    /*
     * The TLS material and the timeouts come from the seed the process was
     * started with: they describe this machine's client, not the deployment,
     * so they are not columns on an instance row.
     */
    options.tls_ca = seed_options_.tls_ca;
    options.tls_cert = seed_options_.tls_cert;
    options.tls_key = seed_options_.tls_key;
    options.request_timeout = seed_options_.request_timeout;
    options.connect_timeout = seed_options_.connect_timeout;

    worker::session_job job;
    job.options = std::move(options);
    job.key = domain::session_key{cmd.instance_id, cmd.account_id};
    job.credential = std::move(credential);

    /*
     * A stored credential travels sealed, with the password that opens it, and
     * the worker opens it on its own thread. The password is copied out of the
     * held secret here and moved into the job, so the job's copy is wiped when
     * the worker is done with it and the held copy survives for the next
     * attempt.
     */
    if (!sealed_credential.empty()) {
        job.sealed_credential = std::move(sealed_credential);
        job.master_password = master_password_;
    }

    const auto key = running_key(cmd.instance_id, cmd.account_id);
    if (const auto previous = running_.find(key); previous != running_.end()) {
        /*
         * A second connect for a pair that is already running asks for a fresh
         * attempt, so the one in flight is told to stop and left to finish on
         * its own: without this its events would arrive for the connection this
         * call is about to replace.
         */
        if (previous->second)
            previous->second->request_stop();
    }
    auto source = std::make_shared<std::stop_source>();
    running_[key] = source;

    /*
     * The credential travels no further than this call. The worker copies it
     * into the attempt and wipes it the moment the sign-in resolves, so there
     * is no second place a secret could be read from.
     */
    worker::spawn_session(pool_.context(), std::move(job), events_, std::move(source));
}

void instance_manager::start_stored_connect(const command::command& cmd,
    std::string sealed) {
    /*
     * The password is copied out of the held secret once, here, and moved into
     * the coroutine. The coroutine wipes its copy when it ends, so this is the
     * second and last copy that exists: the interface never sees one, the
     * session table never holds one, and no event carries one.
     */
    auto password = master_password_;

    const auto instance_id = cmd.instance_id;
    const auto account_id = cmd.account_id;
    const auto principal = cmd.principal;

    boost::asio::co_spawn(pool_.context(),
        [this, instance_id, account_id, principal,
            sealed = std::move(sealed), password = std::move(password)]() mutable
        -> boost::asio::awaitable<void> {
            /*
             * Handed to the pool before anything slow happens. The spawn above
             * may have come from the frame loop, and the derivation below is
             * half a second of deliberate work that must not happen there.
             */
            co_await boost::asio::post(co_await boost::asio::this_coro::executor,
                boost::asio::use_awaitable);

            auto opened = store_crypto::secret_box::open(password.view(), sealed);

            /*
             * The sealed value is wiped whatever happened: it is still a secret
             * in the only sense that matters, because the master password opens
             * it, and an allocation handed back with a credential still in it is
             * a credential left for the next claimant.
             */
            std::fill(sealed.begin(), sealed.end(), '\0');
            sealed.clear();
            sealed.shrink_to_fit();

            /*
             * The password dies here. The derivation is over and nothing below
             * needs it, so it does not stay in this frame while the command is
             * built. @c co_return rather than a fall through, so the wipe is on
             * the success path too and not only the failure one.
             */
            password.clear();

            if (!opened) {
                report_store_failure(instance_id, account_id,
                    std::format("the stored credential for account {} could not "
                                "be opened ({})",
                        account_id, store_crypto::to_string(opened.error().code)));
                co_return;
            }

            command::command retry;
            retry.kind = command::command_kind::connect_session;
            retry.instance_id = instance_id;
            retry.account_id = account_id;
            retry.principal = principal;

            /*
             * The one place a decrypted secret is written outside the worker.
             * It is moved straight into a command, which is where a password an
             * operator typed would have travelled, and the buffer that held it
             * is wiped before this frame ends: the only copy left is the
             * command's, and the worker wipes that the moment the sign-in
             * resolves.
             */
            retry.password = std::move(opened).value();
            commands_.push(std::move(retry));
        },
        boost::asio::detached);
}

void instance_manager::report_store_failure(const std::string& instance_id,
    const std::string& account_id, std::string message) {
    event::event fact;
    fact.kind = event::event_kind::store_lock_failed;
    fact.instance_id = instance_id;
    fact.account_id = account_id;
    fact.lock = store_lock_;
    fact.message = std::move(message);
    events_.push(std::move(fact));
}

void instance_manager::do_set_master_password(const command::command& cmd) {
    if (!master_repo_) {
        last_error_ = "cannot set a master password: the store is not open";
        return;
    }

    /*
     * Refused when one already exists. Replacing it would be worse than
     * useless: the credentials in the store were sealed under the old password,
     * so the store would look healthy and open nothing.
     */
    auto existing = master_repo_->has_master();
    if (!existing) {
        last_error_ = store_failure("cannot read the store's master password",
            existing.error());
        return;
    }
    if (existing.value()) {
        last_error_ =
            "this store already has a master password; unlock it instead";
        return;
    }

    if (cmd.password.empty()) {
        last_error_ = "the master password cannot be empty";
        return;
    }

    /*
     * The sealing is a derivation and therefore slow, so it happens on the pool
     * like every other slow call and reports through the same two events an
     * unlock does. The store ends up unlocked either way: the operator has just
     * chosen the password and holding it is what they asked for.
     */
    auto password = domain::secret_string{cmd.password};

    unlocking_ = true;
    boost::asio::co_spawn(pool_.context(),
        [this, password = std::move(password)]() mutable
        -> boost::asio::awaitable<void> {
            co_await boost::asio::post(co_await boost::asio::this_coro::executor,
                boost::asio::use_awaitable);

            auto created = master_repo_->create(password.view());

            event::event fact;
            if (!created) {
                fact.kind = event::event_kind::store_lock_failed;
                fact.lock = store_lock_;
                fact.message = store_failure(
                    "the master password could not be created", created.error());
            } else {
                fact.kind = event::event_kind::store_unlocked;
                fact.lock = domain::store_lock::unlocked;

                /*
                 * The password is left in the handover slot, not written to the
                 * held secret: assigning that one from here would race the
                 * frame loop, which reads it on every connect. It is written
                 * into the slot before the event is pushed, so the frame loop
                 * cannot see the event without seeing the secret.
                 */
                pending_master_password_ = std::move(password);
            }
            events_.push(std::move(fact));
        },
        boost::asio::detached);
}

void instance_manager::do_unlock_store(const command::command& cmd) {
    if (!master_repo_) {
        last_error_ = "cannot unlock: the store is not open";
        return;
    }

    if (cmd.password.empty()) {
        lock_error_ = "no master password was supplied";
        store_lock_ = domain::store_lock::failed;
        return;
    }

    /*
     * The row is read here, on the frame loop, because it is a cheap query, and
     * the derivation that uses it runs on the pool. Reading it here also means
     * the blob is taken from the store as it is now rather than as it was when
     * the coroutine happened to start.
     */
    auto blob = master_repo_->verification();
    if (!blob) {
        last_error_ = store_failure("cannot read the master password",
            blob.error());
        return;
    }
    if (blob.value().empty()) {
        store_lock_ = domain::store_lock::absent;
        lock_error_ = "this store has no master password yet";
        return;
    }

    auto password = domain::secret_string{cmd.password};
    const auto sealed = std::move(blob).value();

    /*
     * The previous refusal is cleared as the new attempt starts, so the prompt
     * does not show last time's sentence beside this time's password.
     */
    lock_error_.clear();
    unlocking_ = true;
    boost::asio::co_spawn(pool_.context(),
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
                 * The password that worked is left in the handover slot rather
                 * than written to the held secret, because this runs on a pool
                 * thread and the frame loop reads the held secret on every
                 * connect. See the note on @c pending_master_password_.
                 *
                 * The plaintext that came out of the verification blob is the
                 * known sentence and is not needed; it is dropped here, and the
                 * derivation's own buffers are wiped by @c open before it
                 * returns.
                 */
                pending_master_password_ = std::move(password);
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
                fact.lock = store_lock_;
                fact.message = std::format("the store could not be unlocked: {}",
                    opened.error().message);
            }
            events_.push(std::move(fact));
        },
        boost::asio::detached);
}

void instance_manager::do_lock_store() {
    /*
     * Locking is the interest of the operator and never of a failure path, so
     * it always lands on @c locked. Everything sealed under the password becomes
     * unreadable the moment this returns, which is the point of the action.
     */
    lock_error_.clear();
    drop_master_password(true);
}

void instance_manager::drop_master_password(bool deliberate) {
    master_password_.clear();
    store_lock_ = deliberate ? domain::store_lock::locked
                             : domain::store_lock::failed;
}

void instance_manager::do_disconnect_session(const command::command& cmd) {
    stop_session(running_key(cmd.instance_id, cmd.account_id));

    const auto handle = sessions_.find(cmd.instance_id, cmd.account_id);
    if (!handle)
        return;

    /*
     * The windows are unbound before the row is released, because release bumps
     * the generation: after it, the handle a window holds no longer compares
     * equal to the row it was bound to, and the unlink would silently miss.
     */
    for (const auto window : live_windows(windows_)) {
        if (windows_.session_of(window) == *handle)
            windows_.clear_session(window);
    }

    sessions_.remove(*handle);
    if (auto saved = save_windows(); !saved)
        last_error_ = saved.error();
}

void instance_manager::do_open_window(const command::command& cmd) {
    if (!window_repo_) {
        last_error_ = "cannot open a window: the store is not open";
        return;
    }

    domain::window_spec spec;
    spec.id = std::format("w-{}", next_window_serial_++);
    spec.kind = cmd.new_window_kind;

    std::string_view last_error;
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        last_error = sessions_.last_error(*session);
    spec.state = default_state_for(cmd.new_window_kind, last_error);

    const auto handle = windows_.insert(spec);

    /*
     * A window opened from a session is bound to it, so the frame loop can
     * reach its state without a key lookup per window. A window opened with no
     * session named stays unbound and renders the session-loss state.
     */
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        windows_.bind_session(handle, *session);

    if (auto saved = window_repo_->save(windows_.spec(handle, sessions_)); !saved) {
        last_error_ = store_failure(
            std::format("cannot save window {}", spec.id), saved.error());
    }
}

void instance_manager::do_close_window(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle) {
        last_error_ = std::format("cannot close: no window {}", cmd.window_id);
        return;
    }

    windows_.remove(*handle);

    if (window_repo_) {
        if (auto removed = window_repo_->remove(cmd.window_id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove window {}", cmd.window_id),
                removed.error());
        }
    }
}

void instance_manager::do_refresh_window(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle)
        return;

    /*
     * The cadence comes from the window's own persisted description, read back
     * through spec(), rather than from a column: the live table carries what
     * the frame loop reads every frame, and the cadence is not one of those.
     */
    const auto spec = windows_.spec(*handle, sessions_);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto due = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now + spec.cadence);
    windows_.set_next_refresh_ns(*handle, due.count());

    /*
     * Placeholder. A refresh has nothing real to ask for yet, so the request is
     * turned into an event carrying the window id and nothing else, which is
     * enough to exercise the path from a command through the event channel and
     * back into apply(). The real handler replaces the payload, not the shape.
     */
    event::event fact;
    fact.kind = event::event_kind::window_data;
    fact.payload = cmd.window_id;
    events_.push(std::move(fact));
}

void instance_manager::do_probe_instance(const command::command& cmd) {
    /*
     * Forcing a probe moves the deadline rather than starting the probe here:
     * the schedule is then the one place that decides whether an instance is
     * due, and an instance that is already being probed is not probed a second
     * time by an operator who pressed the button twice.
     */
    const auto handle = instances_.find(cmd.instance_id);
    if (!handle) {
        last_error_ = std::format("cannot probe: no instance {}", cmd.instance_id);
        return;
    }

    probes_.set_next_probe_ns(*handle, 0);
}

void instance_manager::do_probe_all() {
    /*
     * Every instance, including the ones whose probe is in flight: moving a
     * deadline is always safe, and the in-flight check in pump_probes() is
     * what stops a sweep from doubling up on an instance.
     */
    const auto& layout = instances_.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        probes_.set_next_probe_ns(
            model_state::instance_handle{index, layout.generation[index]}, 0);
    }
}

void instance_manager::pump_probes() {
    /*
     * A stopped manager starts no probes: shutting down cancels every probe it
     * has, and a pump that outlives the shutdown command must not start a new
     * one behind it.
     */
    if (!started_)
        return;

    const auto now = steady_now_ns();
    const auto& layout = instances_.layout();
    const auto rows = instances_.rows();

    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;

        const auto& row = rows[index];
        if (probing_.contains(row.id))
            continue;

        const model_state::instance_handle handle{index, layout.generation[index]};
        if (probes_.next_probe_ns(handle) > now)
            continue;

        spawn_probe_for(row);
    }
}

void instance_manager::spawn_probe_for(const domain::instance& row) {
    /*
     * Built the way a session builds its own options, from the same seed. The
     * probe is not a session and does not become one by sharing this: what it
     * shares is the description of how to reach the deployment, and the client
     * that uses it is its own.
     */
    transport::session_options options;
    options.nats_url = row.nats_url;
    options.subject_prefix = row.subject_prefix;
    options.wire_format = row.wire_format;
    options.tls_ca = seed_options_.tls_ca;
    options.tls_cert = seed_options_.tls_cert;
    options.tls_key = seed_options_.tls_key;
    options.request_timeout = seed_options_.request_timeout;
    options.connect_timeout = seed_options_.connect_timeout;

    worker::probe_job job;
    job.options = std::move(options);
    job.instance = row;

    auto source = std::make_shared<std::stop_source>();
    probing_[row.id] = source;

    /*
     * The deadline is left where it is while the probe is in flight. It is the
     * answer that moves it, and an instance whose probe never answers is held
     * back by the in-flight entry rather than by a deadline nobody ever
     * clears: a deadline pushed forward on spawn would quietly retire an
     * instance from the schedule if the probe were lost.
     */
    worker::spawn_probe(probe_pool_.context(), std::move(job), events_,
        std::move(source));
}

void instance_manager::apply_probe_result(const event::event& ev) {
    /*
     * The probe is no longer in flight whatever happens next, and leaving the
     * entry behind would retire the instance from the schedule for the rest of
     * the run.
     */
    probing_.erase(ev.instance_id);

    /*
     * An instance that has gone since the probe started has nowhere to put the
     * result, and the handles of the table have moved on. The event is dropped
     * rather than reported: a fact about a removed instance is a fact about
     * nothing, and its slot now belongs to somebody else.
     */
    const auto handle = instances_.find(ev.instance_id);
    if (!handle)
        return;

    domain::probe_result result;
    result.reach = ev.reach;
    result.ready = ev.ready;
    result.latency_us = ev.latency_us;
    result.observed_at_ns = steady_now_ns();
    result.detail = ev.detail;
    probes_.set(*handle, std::move(result));

    /*
     * A probe that got no answer backs the next attempt off; a probe that got
     * an answer -- whatever that answer was -- returns to the cadence. A
     * deployment that is up but not bootstrapped is up, and polling it every
     * ten seconds is how the operator finds out the moment it is set up.
     */
    auto& interval = probe_intervals_[ev.instance_id];
    if (ev.reach == domain::reachability::reachable)
        interval = probe_cadence;
    else
        interval = std::min(interval.count() == 0
                ? probe_cadence * 2
                : interval * 2,
            probe_cadence_max);

    probes_.set_next_probe_ns(*handle,
        steady_now_ns() +
            std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count());
}

void instance_manager::forget_probe(model_state::instance_handle handle,
    const std::string& instance_id) {
    if (const auto found = probing_.find(instance_id); found != probing_.end()) {
        if (found->second)
            found->second->request_stop();
        probing_.erase(found);
    }
    probe_intervals_.erase(instance_id);
    probes_.forget(handle);
}

void instance_manager::apply(const event::event& ev) {
    /*
     * Every event that is not a store event is dealt with below, and the two
     * store events carry their state on the event itself. The handover slot
     * that accompanies @c store_unlocked is therefore read at that event and
     * nowhere else; see the store_unlocked case for why the secret does not
     * ride on the event.
     *
     * A probe's fact is about an instance and not about a session, so it is
     * applied before the session lookup below: routing it through a session
     * key would drop it, because an instance nobody has signed in to has no
     * session to find.
     */
    if (ev.kind == event::event_kind::probe_result) {
        apply_probe_result(ev);
        return;
    }

    /*
     * A store fact is about the whole store and not about a session, so it is
     * applied before the session lookup for the same reason a probe result is:
     * an unlock has no session to be found by.
     */
    if (ev.kind == event::event_kind::store_unlocked) {
        /*
         * The derivation succeeded on the pool, so the password that opened it
         * is now held and every surface that reads the catalogue can be drawn.
         * The password itself never travelled on the event: what arrives is the
         * fact that it was accepted, and the secret is collected from the slot
         * the coroutine left it in.
         */
        unlocking_ = false;
        if (!pending_master_password_.empty())
            master_password_ = std::move(pending_master_password_);
        store_lock_ = domain::store_lock::unlocked;
        last_error_.clear();
        lock_error_.clear();
        return;
    }
    if (ev.kind == event::event_kind::store_lock_failed) {
        unlocking_ = false;

        /*
         * A password that did not verify is dropped rather than kept, so a
         * refused attempt leaves nothing behind to be retried by accident.
         */
        pending_master_password_.clear();

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
            store_lock_ = domain::store_lock::failed;
            /*
             * Beside the prompt and not in the banner. The distinction is the
             * reason @c lock_error exists separately from @c last_error: a
             * refusal is answered by typing something else, and a banner is
             * answered by an administrator.
             */
            lock_error_ = ev.message;
            return;
        }

        if (const auto handle = sessions_.find(ev.instance_id, ev.account_id))
            sessions_.set_last_error(*handle, ev.message);
        return;
    }

    const auto handle = sessions_.find(ev.instance_id, ev.account_id);

    /*
     * A session that is gone is not an error: a worker that was asked to stop
     * may still have one event in flight, and the frame loop applying it to a
     * row that no longer exists would address whoever occupies the slot next.
     */
    if (!handle)
        return;

    switch (ev.kind) {
    case event::event_kind::session_status_changed:
        sessions_.set_status(*handle, ev.status);
        break;
    case event::event_kind::session_authenticated:
        sessions_.set_account_result(*handle, ev.tenant_name, ev.party_name,
            ev.token);
        sessions_.set_status(*handle, domain::session_status::authenticated);
        break;
    case event::event_kind::session_failed: {
        sessions_.set_status(*handle, domain::session_status::failed);

        /*
         * The sentence a person reads stays first and the code follows it in
         * brackets, so the tooltip carries both: the sentence says who to ask,
         * the code says what to tell them.
         */
        sessions_.set_last_error(*handle, ev.code.empty()
                ? ev.message
                : std::format("{} ({})", ev.message, ev.code));
        break;
    }
    case event::event_kind::session_data:
        sessions_.set_latency_us(*handle, ev.latency_us);
        break;
    case event::event_kind::window_data:
        /*
         * Nothing to do: the deadline was already moved when the request was
         * made, and the payload is the window id. A real data handler lands
         * here when there is a data protocol to hand it.
         */
        break;
    case event::event_kind::probe_result:
        /*
         * Never reached: a probe result is applied above, before the session
         * lookup. The case exists so that adding it to the enum without a
         * handler would have been a compile error rather than a silent drop.
         */
        break;
    case event::event_kind::store_unlocked:
    case event::event_kind::store_lock_failed:
        /*
         * Never reached either, and for the same reason: both are applied above
         * where the store is, because neither belongs to a session.
         */
        break;
    }
}

std::expected<void, std::string> instance_manager::save_windows() {
    if (!window_repo_)
        return std::unexpected(std::string("the store is not open"));

    for (const auto& spec : windows_.specs(sessions_)) {
        if (auto saved = window_repo_->save(spec); !saved) {
            last_error_ = store_failure(
                std::format("cannot save window {}", spec.id), saved.error());
            return std::unexpected(last_error_);
        }
    }
    return {};
}

void instance_manager::stop_session(const std::string& window_or_session_id) {
    /*
     * The name of the argument is the interface's: what this stops is the
     * worker registered under the given key, which is the joined session key.
     * Requesting a stop the worker has already observed is a no-op, so a
     * disconnect after a shutdown is safe.
     */
    const auto found = running_.find(window_or_session_id);
    if (found == running_.end())
        return;
    if (found->second)
        found->second->request_stop();
    running_.erase(found);
}

}
