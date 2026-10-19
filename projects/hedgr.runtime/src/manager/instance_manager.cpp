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
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.runtime/seed/seeder.hpp"
#include "hedgr.runtime/service/window_traits.hpp"
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

using condition_source = model_state::condition_source;

using support::lg;
using support::live_indices;
using support::live_windows;
using support::running_key;
using support::steady_now_ns;
using support::store_failure;

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

/**
 * @brief How many probes may be in flight at once.
 *
 * Two, from ADR 5: a slow probe cannot starve the other probes, and at most
 * two connects are in flight at once, which is a bound an operator can reason
 * about when twenty instances are unreachable.
 */
constexpr std::size_t probe_thread_count = 2;

}

instance_manager::instance_manager(std::size_t pool_threads,
    worker::tick_feed_factory market_feeds)
    : pool_(pool_threads), probe_pool_(probe_thread_count)
    , market_feed_(instances_, sessions_, window_service_.windows(),
          read_service_.market(), seed_options_, std::move(market_feeds),
          [this](std::string_view window_id) {
              return read_service_.start_market_catalogue_read(window_id);
          },
          [this](std::string_view instance_id, std::string_view account_id,
                 std::string_view series_id) {
              read_service_.start_market_backfill(instance_id, account_id,
                  series_id);
          }) {
}

instance_manager::~instance_manager() {
    stop();
}

void instance_manager::raise(model_state::condition_key key,
    std::string message) {
    conditions_.raise(std::move(key), model_state::condition_severity::error,
        std::move(message), steady_now_ns());
}

void instance_manager::stop() noexcept {
    if (!started_ && running_.empty() && probe_service_.idle())
        return;

    BOOST_LOG_SEV(lg(), ores::logging::info) << "Stopping the runtime";

    for (auto& [key, source] : running_) {
        if (source)
            source->request_stop();
    }
    running_.clear();
    attempts_.clear();
    market_feed_.cancel();
    read_service_.clear();

    /*
     * The probes are cancelled before the pools are stopped, so a probe
     * waiting on a connect is told the application is going away and does not
     * report an outcome nobody is left to read.
     */
    probe_service_.cancel();

    /*
     * The policy read is cancelled with them: it blocks on the transport too,
     * so a process going away must not leave a pool thread waiting on an
     * answer nobody is left to read.
     */
    policy_service_.cancel();

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
        startup_failure_ = store_failure(
            std::format("cannot open the store at {}", database_path.string()),
            opened.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    database_ = std::make_unique<sqlite::database>(std::move(opened).value());
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Opened the store at " << database_path.string();

    if (auto applied = database_->apply_schema(); !applied) {
        startup_failure_ = store_failure("cannot bring the store up to date",
            applied.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
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
    shortcut_repo_ = std::make_unique<store::repository::shortcut_repository>(
        *database_);
    window_service_.attach(*database_);
    lock_service_.attach(*database_, *account_repo_);
    read_service_.attach(database_->path());
    market_feed_.attach(database_->path());

    /*
     * The lock is read before anything else, because it says whether the rows
     * below can be trusted to mean anything: the whole catalogue sits behind
     * the master password, so a store with a master row that is not held is a
     * store whose contents are ciphertext. @c absent means a first launch, and
     * the interface offers to create one; @c locked means the operator has been
     * here before and is asked for the password. Neither is a failure, and
     * neither stops the start.
     */
    if (auto read = lock_service_.load(); !read) {
        startup_failure_ = std::move(read.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }

    /*
     * The seed carries what is the same for every instance -- the timeouts, and
     * the TLS material of an instance that has none of its own. It is kept, not
     * just read, because a session opened an hour from now must fall back to
     * the same material it would have been built from now.
     */
    seed_options_ = seed;

    auto instances = instance_repo_->list();
    if (!instances) {
        startup_failure_ = store_failure("cannot read the stored instances",
            instances.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    instances_.clear();
    for (const auto& row : instances.value())
        static_cast<void>(instances_.upsert(row));

    /*
     * Immediately after the read and before anything that could depend on it,
     * so a store written by a build that lost the keys has them back for this
     * process, not the next one. It is a start step rather than a migration
     * because it has to reach every store, including ones already stamped 8,
     * and because a migration runs once where this runs every time.
     */
    const seed::tables seed_tables{instances_, accounts_, folders_, labels_,
        labellings_};
    const seed::repositories seed_repositories{instance_repo_.get(),
        account_repo_.get(), folder_repo_.get(), label_repo_.get(),
        labelling_repo_.get()};
    for (auto& refused :
        seed::backfill_seeded_icon_keys(seed_tables, seed_repositories))
        raise(std::move(refused.key), std::move(refused.message));

    auto accounts = account_repo_->list();
    if (!accounts) {
        startup_failure_ = store_failure("cannot read the stored accounts",
            accounts.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    accounts_.clear();

    /*
     * Whether each account can sign in without a prompt is a fact about the
     * local store, read once here as a list of ids. The credentials themselves
     * are not read: the interface only has to know which accounts have one.
     */
    const auto with_credentials = account_repo_->ids_with_credential();
    if (!with_credentials) {
        startup_failure_ = store_failure("cannot read the stored credentials",
            with_credentials.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    const std::unordered_set<std::string> credential_ids(
        with_credentials->begin(), with_credentials->end());
    for (const auto& row : accounts.value()) {
        auto copy = row;
        copy.has_credential = credential_ids.contains(row.id);
        static_cast<void>(accounts_.upsert(copy));
    }

    auto folders = folder_repo_->list();
    if (!folders) {
        startup_failure_ = store_failure("cannot read the stored folders",
            folders.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    folders_.clear();
    for (const auto& row : folders.value())
        static_cast<void>(folders_.upsert(row));

    auto labels = label_repo_->list();
    if (!labels) {
        startup_failure_ = store_failure("cannot read the stored labels",
            labels.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    labels_.clear();
    for (const auto& row : labels.value())
        static_cast<void>(labels_.upsert(row));

    auto labellings = labelling_repo_->list();
    if (!labellings) {
        startup_failure_ = store_failure("cannot read the stored label attachments",
            labellings.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    labellings_.clear();
    for (const auto& row : labellings.value())
        labellings_.add(row.owner, row.owner_id, row.label_id);

    /*
     * The bar is read in the order it was stored in, and the table keeps that
     * order rather than a caller sorting it, because position and id are what
     * the bar draws by and the read already used them.
     */
    auto shortcuts = shortcut_repo_->list();
    if (!shortcuts) {
        startup_failure_ = store_failure("cannot read the stored shortcuts",
            shortcuts.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    shortcuts_.clear();
    for (const auto& row : shortcuts.value())
        shortcuts_.upsert(row);

    if (auto loaded = window_service_.load(); !loaded) {
        startup_failure_ = loaded.error();
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }

    for (auto& refused : seed::seed_store(seed_tables, seed_repositories,
             seed_options_))
        raise(std::move(refused.key), std::move(refused.message));
    window_service_.ensure_instance_manager_window();

    /*
     * A seed the store refused is a start failure like any other: the next run
     * would not find the instance and would seed it again, so reporting it now
     * is what stops the interface from showing an instance that no store has.
     */
    if (const auto* refused = conditions_.current()) {
        startup_failure_ = refused->message;
        return std::unexpected(startup_failure_);
    }

    started_ = true;
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Runtime ready with " << instances_.size() << " instances and "
        << accounts_.size() << " accounts";
    return {};
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
    probe_service_.pump_probes(started_, [this](const domain::instance& row) {
        return connection_options_for(row, seed_options_, database_->path());
    });
    read_service_.pump_crm_reads();

    /*
     * Then the policy read, once per process. The create prompt is drawn from
     * the first frame, so the sooner the question is asked the sooner that
     * screen can say something other than that it is waiting; and asking on
     * the first pump rather than in start() keeps the answer on the same event
     * channel as every other worker's.
     */
    if (!policy_service_.requested())
        policy_service_.fetch_password_policy();

    /*
     * Watches follow the open chart windows, and the buckets the subscriptions
     * closed since the last frame are applied here. Both happen after the
     * events, so a session that authenticated this frame has its stream.
     */
    for (auto& refused : market_feed_.pump())
        raise(std::move(refused.key), std::move(refused.message));
}

std::string instance_manager::open_accounts_window(const std::string& instance_id) {
    /*
     * The same rule as every other window opened by the interface, reached
     * directly because this one is asked for by a caller that has no command
     * queue to push to: the id is derived from the instance, and one lookup
     * answers whether the window exists.
     */
    const std::string wanted = service::window_key_for(
        domain::window_kind::accounts, instance_id, {}, {});
    if (window_service_.show_existing(wanted))
        return wanted;

    command::command request;
    request.kind = command::command_kind::open_window;
    request.instance_id = instance_id;
    request.new_window_kind = domain::window_kind::accounts;
    handle(request);
    return wanted;
}

void instance_manager::handle(command::command& cmd) {
    /*
     * The verdicts on the previous command go as the next request starts, so
     * what the interface shows is the outcome of the last thing the operator
     * asked for and not the outcome of something they asked for a minute ago.
     * Without this a note would only ever be written and never read back
     * down: a removal that succeeded after one that was refused would leave
     * the bar reporting the refusal, which is a failure the store did not have.
     *
     * Cleared for every command and not only for the ones that can fail, so a
     * command that cannot fail -- a window opening, a probe -- also takes down
     * a stale note. A condition the handler then raises lands after this line
     * and survives, which is what makes the bar's red mean "the last thing you
     * did did not work". What stays is a standing condition, such as a wall
     * the store would not save: that is still true whatever the operator does
     * next, and only the save that succeeds clears it.
     */
    conditions_.clear_verdicts();

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Command: " << hedgr::model::command::to_string(cmd.kind)
        << (cmd.instance_id.empty() ? std::string{}
                                    : std::format(" instance '{}'", cmd.instance_id))
        << (cmd.account_id.empty() ? std::string{}
                                   : std::format(" account '{}'", cmd.account_id))
        << (cmd.window_id.empty() ? std::string{}
                                  : std::format(" window '{}'", cmd.window_id));

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
    case command::command_kind::save_shortcut:
        do_save_shortcut(cmd);
        break;
    case command::command_kind::remove_shortcut:
        do_remove_shortcut(cmd);
        break;
    case command::command_kind::connect_session:
        do_connect_session(cmd);
        break;
    case command::command_kind::store_credential:
        if (auto refused = lock_service_.store_credential(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::load_image:
        read_service_.load_image(cmd);
        break;
    case command::command_kind::load_term_structure:
        read_service_.load_term_structure(cmd);
        break;
    case command::command_kind::disconnect_session:
        do_disconnect_session(cmd);
        break;
    case command::command_kind::open_window:
        complete_open(window_service_.open(cmd));
        break;
    case command::command_kind::close_window:
        if (auto refused = window_service_.close(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::refresh_window:
        do_refresh_window(cmd);
        break;
    case command::command_kind::configure_crm:
        if (window_service_.configure_crm(cmd))
            static_cast<void>(save_windows());
        break;
    case command::command_kind::load_crm_names:
        read_service_.load_crm_names(cmd);
        break;
    case command::command_kind::probe_instance:
        if (auto refused = probe_service_.do_probe_instance(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::probe_all:
        probe_service_.do_probe_all();
        break;
    case command::command_kind::set_master_password:
        if (auto refused = lock_service_.set_master_password(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::unlock_store:
        if (auto refused = lock_service_.unlock_store(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::lock_store:
        lock_service_.lock_store();
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
        probe_service_.cancel();
        started_ = false;
        break;
    }

    /*
     * The password is wiped once the command has been acted on, on every path
     * including the ones that refused it. Nothing below this point needs it --
     * a credential is handed to a worker during the dispatch above, not after
     * it -- so the only thing a surviving copy could do is be read by whoever
     * gets that allocation next.
     */
    cmd.password.clear();

    /*
     * The sealed credential is ciphertext, and it is still not left in a buffer
     * that outlives the frame. The store has its own copy now.
     */
    if (cmd.kind == command::command_kind::store_credential) {
        std::fill(cmd.payload.begin(), cmd.payload.end(), '\0');
        cmd.payload.clear();
        cmd.payload.shrink_to_fit();
    }
}

void instance_manager::do_save_instance(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save instance " << cmd.instance_row.name << "";
    if (!instance_repo_) {
        raise({condition_source::instance, cmd.instance_row.id},
            "cannot save an instance: the store is not open");
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

    /*
     * An instance with no folder named lands in the instances tree's root,
     * which is where a row created before folders existed belongs. Without this
     * it would be in no folder at all, and the tree would draw a tree that
     * omitted it.
     */
    domain::instance row = cmd.instance_row;
    if (row.folder_id.empty())
        row.folder_id = std::string{domain::instances_root_id};

    const auto handle = instances_.upsert(row);

    if (auto saved = instance_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(instances_.upsert(*previous));
        else
            instances_.remove(handle);
        raise({condition_source::instance, row.id},
            store_failure(
                std::format("cannot save instance {}", row.id), saved.error()));
        return;
    }

    /*
     * The instance is stored, so it needs somewhere for its accounts to live.
     * The root is created here rather than lazily on the first account, because
     * a tree with no root is a shape the Browser cannot draw.
     */
    /*
     * The row's own id, not the command's: the row is what was stored, and a
     * caller that named the id only in the command's field would otherwise get
     * an accounts tree rooted at nothing. The two are the same value for every
     * caller that fills both, which is why only one of them is read here.
     */
    if (auto refused =
            seed::ensure_accounts_root(folders_, folder_repo_.get(), row.id))
        raise(std::move(refused->key), std::move(refused->message));
}

void instance_manager::do_remove_instance(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove instance " << cmd.instance_id << "";
    /*
     * The interface names the row by id and the row itself also carries one, so
     * either is accepted rather than dropping a removal that named the
     * instance in the field the other caller used.
     */
    const std::string id = cmd.instance_id.empty() ? cmd.instance_row.id
                                                   : cmd.instance_id;
    if (id.empty()) {
        raise({condition_source::instance, id},
            "cannot remove an instance: no instance was named");
        return;
    }

    if (const auto handle = instances_.find(id)) {
        /*
         * Everything the schedule holds for this instance goes with it, and
         * the probe already in flight is cancelled: a result for a removed
         * instance would be applied to whatever takes its slot next, and the
         * deadline would keep a probe running for a row nobody can read.
         */
        probe_service_.forget_probe(*handle, id);
        instances_.remove(*handle);
    }

    /*
     * The cascade is a walk rather than a lookup: what the accounts table is
     * asked is which rows name this instance, and its handles are what a
     * removal takes. Each account takes its own shortcuts first, because after
     * the row is released the id it was keyed by is gone.
     */
    for (const auto account : live_accounts(accounts_)) {
        if (accounts_.get(account)->instance_id != id)
            continue;
        const std::string account_id = accounts_.get(account)->id;
        drop_target_shortcuts(domain::window_kind::workspace, account_id);
        accounts_.remove(account);
    }

    /*
     * The accounts window of this instance is a shortcut target of its own, and
     * it goes with the instance for the same reason its accounts do.
     */
    drop_target_shortcuts(domain::window_kind::accounts, id);

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
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the folders of instance {}", id),
                    removed.error()));
        }
    }

    /* A label attachment is the instance's, so it goes with the instance. */
    labellings_.remove_owner(domain::labelling_owner::instance, id);
    if (labelling_repo_) {
        if (auto removed = labelling_repo_->remove_owner(
                domain::labelling_owner::instance, id);
            !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the labels of instance {}", id),
                    removed.error()));
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
        window_service_.unbind(session);

        sessions_.remove(session);
    }

    if (instance_repo_) {
        if (auto removed = instance_repo_->remove(id); !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove instance {}", id), removed.error()));
        }
    }
    if (account_repo_) {
        if (auto removed = account_repo_->remove_for_instance(id); !removed) {
            raise({condition_source::instance, id},
                store_failure(
                    std::format("cannot remove the accounts of instance {}", id),
                    removed.error()));
        }
    }
    static_cast<void>(save_windows());
}

void instance_manager::do_save_account(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save account " << cmd.account_row.principal << "";
    if (!account_repo_) {
        raise({condition_source::account, cmd.account_row.id},
            "cannot save an account: the store is not open");
        return;
    }

    const auto existing = accounts_.find(cmd.account_row.id);
    const auto previous = existing
        ? std::optional<domain::account>(*accounts_.get(*existing))
        : std::nullopt;

    /*
     * An account with no folder named lands in the accounts root of its own
     * instance, which is the one folder every account of that instance may sit
     * in. The root is derived from the instance rather than looked up, so a
     * save that races the root's creation still has somewhere to put the row.
     */
    domain::account row = cmd.account_row;
    /*
     * The flag is not part of a saved row, so an edit that built its value from
     * the form would clear it and make the next sign-in ask for a password the
     * store already holds.
     */
    if (previous)
        row.has_credential = previous->has_credential;
    if (row.folder_id.empty())
        row.folder_id = domain::accounts_root_id_for(row.instance_id);

    const auto handle = accounts_.upsert(row);

    if (auto saved = account_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(accounts_.upsert(*previous));
        else
            accounts_.remove(handle);
        raise({condition_source::account, row.id},
            store_failure(
                std::format("cannot save account {}", row.id), saved.error()));
    }
}

void instance_manager::do_remove_account(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove account " << cmd.account_id << "";
    const std::string id = cmd.account_id.empty() ? cmd.account_row.id
                                                  : cmd.account_id;
    if (id.empty()) {
        raise({condition_source::account, id},
            "cannot remove an account: no account was named");
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

    /*
     * A shortcut naming this account as its workspace target goes with it. The
     * entry is dropped by the same command that removed the row it opens, which
     * is what stops the bar from offering a surface whose account is gone.
     */
    drop_target_shortcuts(domain::window_kind::workspace, id);

    if (const auto session = sessions_.find(instance_id, id)) {
        stop_session(running_key(instance_id, id));
        market_feed_.close(instance_id, id);
        window_service_.unbind(*session);
        sessions_.remove(*session);
    }

    if (account_repo_) {
        if (auto removed = account_repo_->remove(id); !removed) {
            raise({condition_source::account, id},
                store_failure(
                    std::format("cannot remove account {}", id), removed.error()));
        }
    }
    static_cast<void>(save_windows());
}

void instance_manager::do_save_folder(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save folder " << cmd.folder_row.name << "";
    if (!folder_repo_) {
        raise({condition_source::folder, cmd.folder_row.id},
            "cannot save a folder: the store is not open");
        return;
    }

    const auto& incoming = cmd.folder_row;
    if (incoming.id.empty()) {
        raise({condition_source::folder, incoming.id},
            "cannot save a folder: no folder was named");
        return;
    }
    if (incoming.id == domain::instances_root_id) {
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot re-parent folder {}: it is the root of the instances tree",
                incoming.id));
        return;
    }
    if (incoming.name.empty()) {
        raise({condition_source::folder, incoming.id},
            std::format("cannot save folder {}: it has no name",
                incoming.id));
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
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot save folder {}: no parent folder {}", incoming.id,
                incoming.parent_id.empty() ? "<none>" : incoming.parent_id));
        return;
    }
    if (row.parent_id.empty()) {
        raise({condition_source::folder, incoming.id},
            std::format(
                "cannot save folder {}: only a root has no parent", incoming.id));
        return;
    }

    /*
     * A cycle is refused by the table as well, because a table is reachable
     * without this handler. It is reported here too so that the sentence names
     * the folder the operator asked about rather than the id the table saw.
     */
    if (row.id == row.parent_id) {
        raise({condition_source::folder, row.id},
            std::format("cannot parent folder {} to itself", row.id));
        return;
    }

    auto handle = folders_.upsert(row);
    if (!handle) {
        raise({condition_source::folder, row.id}, handle.error());
        return;
    }

    if (auto saved = folder_repo_->save(row); !saved) {
        if (previous)
            static_cast<void>(folders_.upsert(*previous));
        else
            folders_.remove(*handle);
        raise({condition_source::folder, row.id},
            store_failure(
                std::format("cannot save folder {}", row.id), saved.error()));
    }
}

void instance_manager::do_remove_folder(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove folder " << cmd.folder_row.id << "";
    if (!folder_repo_) {
        raise({condition_source::folder, cmd.folder_row.id},
            "cannot remove a folder: the store is not open");
        return;
    }

    const std::string id = cmd.folder_row.id.empty() ? cmd.payload
                                                    : cmd.folder_row.id;
    if (id.empty()) {
        raise({condition_source::folder, id},
            "cannot remove a folder: no folder was named");
        return;
    }

    /*
     * The root of a tree is what the tree is anchored by: removing it would
     * leave every folder below it unreachable, so the refusal is here in the
     * runtime and not only in whatever surfaced the request.
     */
    if (id == domain::instances_root_id) {
        raise({condition_source::folder, id},
            std::format(
                "cannot remove folder {}: it is the root of the instances tree", id));
        return;
    }
    if (const auto handle = folders_.find(id)) {
        if (folders_.get(*handle)->parent_id.empty()) {
            raise({condition_source::folder, id},
                std::format(
                    "cannot remove folder {}: it is a root of its tree", id));
            return;
        }
    }

    /*
     * The children are collected before anything goes, because a removal
     * releases the slot and a walk from a released row would find nothing.
     */
    const auto doomed = folders_.descendants_of(id);
    auto going = doomed;
    going.push_back(id);

    /*
     * Removing a folder removes what is under it and does not take the leaves
     * with it: an operator who deletes a folder wants the folder gone, not
     * their instances deleted. The leaves are re-homed to their tree's root
     * first, which is the one folder every leaf of a tree may sit in, so no
     * leaf is ever left naming a folder that no longer exists.
     */
    rehome_leaves(going);

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
        raise({condition_source::folder, id},
            store_failure(
                std::format("cannot remove folder {}", id), removed.error()));
    }
    for (const auto& folder_id : doomed) {
        if (auto removed = folder_repo_->remove(folder_id); !removed) {
            raise({condition_source::folder, folder_id},
                store_failure(
                    std::format("cannot remove folder {}", folder_id),
                    removed.error()));
        }
    }
}

void instance_manager::rehome_leaves(const std::vector<std::string>& folder_ids) {
    const auto inside = [&](std::string_view folder_id) {
        return !folder_id.empty()
            && std::find(folder_ids.begin(), folder_ids.end(), folder_id)
                != folder_ids.end();
    };

    const auto root_of = [&](domain::folder_tree tree,
                             std::string_view owner_id) -> std::optional<std::string> {
        const auto root = folders_.root(tree, owner_id);
        if (!root)
            return std::nullopt;
        return folders_.get(*root)->id;
    };

    for (const auto index : live_indices(instances_.layout())) {
        const auto handle = model_state::instance_handle{
            index, instances_.layout().generation[index]};
        const auto* row = instances_.get(handle);
        if (row == nullptr || !inside(row->folder_id))
            continue;

        const auto root = root_of(domain::folder_tree::instances, {});
        if (!root) {
            raise({condition_source::instance, row->id},
                std::format(
                    "the instances tree has no root, so {} cannot be moved out of "
                    "the folder being removed",
                    row->id));
            continue;
        }

        instances_.set_folder(handle, *root);
        if (instance_repo_) {
            if (auto moved = instance_repo_->set_parent(row->id, *root); !moved) {
                raise({condition_source::instance, row->id},
                    store_failure(
                        std::format("cannot move instance {} out of a removed "
                                    "folder",
                            row->id),
                        moved.error()));
            }
        }
    }

    for (const auto index : live_indices(accounts_.layout())) {
        const auto handle = model_state::account_handle{
            index, accounts_.layout().generation[index]};
        const auto* row = accounts_.get(handle);
        if (row == nullptr || !inside(row->folder_id))
            continue;

        const auto root = root_of(domain::folder_tree::accounts, row->instance_id);
        if (!root) {
            raise({condition_source::account, row->id},
                std::format(
                    "instance {} has no accounts root, so account {} cannot be "
                    "moved out of the folder being removed",
                    row->instance_id, row->id));
            continue;
        }

        accounts_.set_folder(handle, *root);
        if (account_repo_) {
            if (auto moved = account_repo_->set_parent(row->id, *root); !moved) {
                raise({condition_source::account, row->id},
                    store_failure(
                        std::format("cannot move account {} out of a removed "
                                    "folder",
                            row->id),
                        moved.error()));
            }
        }
    }
}

void instance_manager::do_save_label(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save label " << cmd.label_row.name << "";
    if (!label_repo_) {
        raise({condition_source::label, cmd.label_row.id},
            "cannot save a label: the store is not open");
        return;
    }

    const auto& row = cmd.label_row;
    if (row.id.empty()) {
        raise({condition_source::label, row.id},
            "cannot save a label: no label was named");
        return;
    }
    if (row.name.empty()) {
        raise({condition_source::label, row.id},
            std::format("cannot save label {}: it has no name",
                row.id));
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
        raise({condition_source::label, row.id},
            store_failure(
                std::format("cannot save label {}", row.id), saved.error()));
    }
}

void instance_manager::do_remove_label(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove label " << cmd.label_row.id << "";
    if (!label_repo_) {
        raise({condition_source::label, cmd.label_row.id},
            "cannot remove a label: the store is not open");
        return;
    }

    const std::string id = cmd.label_row.id.empty() ? cmd.payload
                                                   : cmd.label_row.id;
    if (id.empty()) {
        raise({condition_source::label, id},
            "cannot remove a label: no label was named");
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
            raise({condition_source::label, id},
                store_failure(
                    std::format("cannot detach label {} from {}", id, row.owner_id),
                    detached.error()));
        }
    }

    if (auto removed = label_repo_->remove(id); !removed) {
        raise({condition_source::label, id},
            store_failure(
                std::format("cannot remove label {}", id), removed.error()));
    }
}

void instance_manager::do_attach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        raise({condition_source::labelling, cmd.labelling_row.owner_id},
            "cannot attach a label: the store is not open");
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        raise({condition_source::labelling, row.owner_id},
            "cannot attach a label: the owner and the label must be "
                          "named");
        return;
    }

    /*
     * Both ends are resolved before the attachment is written: an attachment to
     * a row that does not exist is a foreign key the store would refuse, and
     * refusing it here says which end was missing.
     */
    if (row.owner == domain::labelling_owner::instance) {
        if (!instances_.find(row.owner_id)) {
            raise({condition_source::labelling, row.owner_id},
                std::format("cannot attach a label: no instance {}",
                    row.owner_id));
            return;
        }
    } else if (!accounts_.find(row.owner_id)) {
        raise({condition_source::labelling, row.owner_id},
            std::format("cannot attach a label: no account {}",
                row.owner_id));
        return;
    }
    if (!labels_.find(row.label_id)) {
        raise({condition_source::labelling, row.owner_id},
            std::format("cannot attach a label: no label {}",
                row.label_id));
        return;
    }

    labellings_.add(row.owner, row.owner_id, row.label_id);

    if (auto saved = labelling_repo_->save(row); !saved) {
        labellings_.remove(row.owner, row.owner_id, row.label_id);
        raise({condition_source::labelling, row.owner_id},
            store_failure(
                std::format("cannot attach label {} to {}", row.label_id,
                    row.owner_id),
                saved.error()));
    }
}

void instance_manager::do_detach_label(const command::command& cmd) {
    if (!labelling_repo_) {
        raise({condition_source::labelling, cmd.labelling_row.owner_id},
            "cannot detach a label: the store is not open");
        return;
    }

    const auto& row = cmd.labelling_row;
    if (row.owner_id.empty() || row.label_id.empty()) {
        raise({condition_source::labelling, row.owner_id},
            "cannot detach a label: the owner and the label must be "
                          "named");
        return;
    }

    labellings_.remove(row.owner, row.owner_id, row.label_id);

    if (auto removed = labelling_repo_->remove(row); !removed) {
        raise({condition_source::labelling, row.owner_id},
            store_failure(
                std::format("cannot detach label {} from {}", row.label_id,
                    row.owner_id),
                removed.error()));
    }
}

void instance_manager::do_save_shortcut(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Save shortcut " << cmd.shortcut_row.id << "";
    if (!shortcut_repo_) {
        raise({condition_source::shortcut, cmd.shortcut_row.id},
            "cannot save a shortcut: the store is not open");
        return;
    }

    const auto& row = cmd.shortcut_row;
    if (row.id.empty()) {
        raise({condition_source::shortcut, row.id},
            "cannot save a shortcut: no shortcut was named");
        return;
    }

    /*
     * The entry is read back before it is replaced, so a store that refuses the
     * write leaves the bar as it was. A bar still offering the previous entry
     * is the truth; one showing an entry no store accepted is a lie the next
     * start would contradict.
     */
    std::optional<domain::shortcut> previous;
    for (const auto& existing : shortcuts_.rows()) {
        if (existing.id == row.id) {
            previous = existing;
            break;
        }
    }

    shortcuts_.upsert(row);

    if (auto saved = shortcut_repo_->save(row); !saved) {
        if (previous)
            shortcuts_.upsert(*previous);
        else
            shortcuts_.remove(row.id);
        raise({condition_source::shortcut, row.id},
            store_failure(
                std::format("cannot save shortcut {}", row.id), saved.error()));
    }
}

void instance_manager::do_remove_shortcut(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info) << "Remove shortcut " << cmd.shortcut_row.id << "";
    if (!shortcut_repo_) {
        raise({condition_source::shortcut, cmd.shortcut_row.id},
            "cannot remove a shortcut: the store is not open");
        return;
    }

    const std::string id = cmd.shortcut_row.id.empty() ? cmd.payload
                                                       : cmd.shortcut_row.id;
    if (id.empty()) {
        raise({condition_source::shortcut, id},
            "cannot remove a shortcut: no shortcut was named");
        return;
    }

    shortcuts_.remove(id);

    if (auto removed = shortcut_repo_->remove(id); !removed) {
        raise({condition_source::shortcut, id},
            store_failure(
                std::format("cannot remove shortcut {}", id), removed.error()));
    }
}

void instance_manager::drop_target_shortcuts(domain::window_kind kind,
    const std::string& target_id) {
    /*
     * An empty target id is the Instances window and a handful of other kinds
     * that concern no row. Nothing was removed, so nothing is dropped: treating
     * empty as a match would empty the bar every time an instance went.
     */
    if (target_id.empty())
        return;

    shortcuts_.remove_target(kind, target_id);

    if (!shortcut_repo_)
        return;
    if (auto removed = shortcut_repo_->remove_for_target(kind, target_id);
        !removed) {
        raise({condition_source::shortcut, target_id},
            store_failure(
                std::format("cannot remove the shortcuts for target {}", target_id),
                removed.error()));
    }
}

void instance_manager::do_connect_session(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Sign-in requested for '" << cmd.account_id << "' on '"
        << cmd.instance_id << "'";

    /*
     * Both ids arrive as text from the interface, so both are resolved here:
     * the instance's row is read for the connection settings, and the account
     * has to exist, because its identity is also what a stored credential is
     * looked up by.
     */
    const auto instance = instances_.find(cmd.instance_id);
    if (!instance) {
        raise({condition_source::session, cmd.account_id},
            std::format("cannot connect: no instance {}",
                cmd.instance_id));
        return;
    }
    const auto* instance_row = instances_.get(*instance);

    if (!accounts_.find(cmd.account_id)) {
        raise({condition_source::session, cmd.account_id},
            std::format("cannot connect: no account {}",
                cmd.account_id));
        return;
    }

    /*
     * A credential typed for this attempt wins over a stored one. The operator
     * who has just filled in a password prompt is asking for that password to
     * be used, and quietly preferring the remembered one would make the prompt
     * a lie.
     */
    if (!cmd.password.empty()) {
        if (cmd.remember && lock_service_.store_lock() == domain::store_lock::unlocked) {
            lock_service_.remember(cmd.account_id,
                std::string{cmd.password.view()});
        } else if (cmd.remember) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Not remembering the password for '" << cmd.account_id
                << "': the store is not unlocked";
        }
        start_session_for(cmd,
            domain::credential{cmd.principal, std::string{cmd.password.view()}});
        return;
    }

    /*
     * No password and a closed lock is a refusal, and the sentence says which
     * of the two things went wrong. "The password was wrong" would be wrong:
     * nothing was tried, and an operator who is told their password is bad will
     * retype a password that was never the problem.
     */
    if (lock_service_.store_lock() != domain::store_lock::unlocked) {
        raise({condition_source::store_lock, cmd.account_id},
            std::format(
                "cannot connect to {}: the store is {}, so no password is "
                "available and none was supplied",
                instance_row->name,
                domain::to_string(lock_service_.store_lock())));
        return;
    }

    auto sealed = account_repo_->credential_of(cmd.account_id);
    if (!sealed) {
        raise({condition_source::credential, cmd.account_id},
            store_failure("cannot read the stored credential",
                sealed.error()));
        return;
    }
    if (!sealed.value()) {
        raise({condition_source::credential, cmd.account_id},
            std::format(
                "cannot connect to {}: the account has no stored credential and "
                "no password was supplied",
                instance_row->name));
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Cannot sign in to '" << instance_row->name
            << "': account '" << cmd.account_id
            << "' has no stored credential and no password was supplied";
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

    transport::session_options options =
        connection_options_for(*instance_row, seed_options_, database_->path());

    worker::session_job job;
    job.options = std::move(options);
    job.key = domain::session_key{cmd.instance_id, cmd.account_id};
    job.credential = std::move(credential);

    const auto attempt_key = running_key(cmd.instance_id, cmd.account_id);
    const auto attempt = ++next_attempt_;
    attempts_[attempt_key] = attempt;
    job.attempt = attempt;

    /*
     * A stored credential travels sealed, with the password that opens it, and
     * the worker opens it on its own thread. The password is copied out of the
     * held secret here and moved into the job, so the job's copy is wiped when
     * the worker is done with it and the held copy survives for the next
     * attempt.
     */
    if (!sealed_credential.empty()) {
        job.sealed_credential = std::move(sealed_credential);
        job.master_password = lock_service_.master_password();
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
    auto password = lock_service_.master_password();

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
            retry.password = domain::secret_string{std::move(opened).value()};
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
    fact.message = std::move(message);
    events_.push(std::move(fact));
}

void instance_manager::do_disconnect_session(const command::command& cmd) {
    stop_session(running_key(cmd.instance_id, cmd.account_id));
    market_feed_.close(cmd.instance_id, cmd.account_id);

    const auto handle = sessions_.find(cmd.instance_id, cmd.account_id);
    if (!handle)
        return;

    /*
     * The windows are unbound before the row is released, because release bumps
     * the generation: after it, the handle a window holds no longer compares
     * equal to the row it was bound to, and the unlink would silently miss.
     */
    window_service_.unbind(*handle);

    sessions_.remove(*handle);
    static_cast<void>(save_windows());
}

void instance_manager::do_refresh_window(const command::command& cmd) {
    const auto request = window_service_.refresh(cmd);
    if (!request)
        return;

    read_service_.retry_currency_image_map(cmd.instance_id, cmd.account_id);

    start_read(*request);
}

void instance_manager::start_read(const service::read_request& request) {
    if (auto refused = read_service_.start(request))
        raise(std::move(refused->key), std::move(refused->message));
}

void instance_manager::complete_open(service::open_outcome outcome) {
    if (outcome.read)
        start_read(*outcome.read);
    if (outcome.refused)
        raise(std::move(outcome.refused->key), std::move(outcome.refused->message));
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
        probe_service_.apply_probe_result(ev);
        return;
    }

    /*
     * A policy answer is about the deployment and not about a session, so it
     * is applied here for the same reason a probe result is: there is no
     * session to find it by, and the screen waiting on it is the create prompt,
     * which is drawn before anybody has signed in to anything.
     */
    if (ev.kind == event::event_kind::password_policy_fetched) {
        policy_service_.apply_password_policy(ev);
        return;
    }

    /*
     * A store fact is about the whole store and not about a session, so it is
     * applied before the session lookup for the same reason a probe result is:
     * an unlock has no session to be found by.
     */
    if (service::is_lock_event(ev.kind)) {
        const auto effect = lock_service_.apply(ev);
        if (effect.unlocked)
            conditions_.clear(condition_source::store_lock);
        if (effect.credential) {
            if (const auto handle = sessions_.find(effect.credential->instance_id,
                    effect.credential->account_id))
                sessions_.set_last_error(*handle, effect.credential->message);
        }
        return;
    }

    /*
     * A read's answer is applied before the session lookup. The caches it
     * fills are keyed by name rather than by the session row, and the answer
     * is the only thing that clears the read's in-flight mark: dropping it
     * because the operator signed out meanwhile would leave the read marked as
     * running, and refuse it again after the next sign-in.
     */
    if (read_service_.apply(ev))
        return;

    /*
     * A fact from a sign-in attempt that has since been replaced is not news
     * about the session that is live now. The stopped worker may still be
     * finishing, and its disconnect would otherwise land on top of the newer
     * worker's authenticated state.
     */
    if (ev.attempt != 0) {
        const auto current = attempts_.find(
            running_key(ev.instance_id, ev.account_id));
        if (current == attempts_.end() || current->second != ev.attempt)
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
            ev.party_id, ev.token);
        sessions_.set_tenant_id(*handle, ev.tenant_id);
        sessions_.set_parties(*handle, ev.parties);
        sessions_.set_mode(*handle, ev.mode);
        sessions_.set_status(*handle, domain::session_status::authenticated);
        market_feed_.open(ev);
        read_service_.start_currency_image_map(ev);

        /*
         * Stamped here rather than by the worker, so the time says when the
         * frame loop accepted the sign-in and every session's clock reading
         * comes from one clock. A re-authentication of a pair that already had
         * a session restamps it, which is what makes the workspace show the age
         * of the connection it is actually looking at.
         */
        sessions_.set_opened_at_ns(*handle, steady_now_ns());

        /*
         * The landing surface is decided by the fact and not by the click that
         * asked for it: a super administrator's session belongs to the system
         * tenant, and that is known only once IAM has answered. Opening it here
         * also means a refused sign-in leaves no window behind.
         */
        complete_open(window_service_.open_landing(ev.instance_id,
            ev.account_id,
            sessions_.mode(*handle) == domain::session_mode::system_administration
                ? domain::window_kind::command_centre
                : domain::window_kind::workspace));

        /*
         * Every other window the wall restored was saved against this same
         * pair and is still unbound. Binding them here is what makes a
         * restored series browser or chart live on the sign-in that owns it.
         */
        window_service_.rebind(ev.instance_id, ev.account_id, *handle);
        break;
    case event::event_kind::session_token_refreshed:
        /*
         * A refresh replaces the credential, not the session. Reads that start
         * from here on take the new token, and the tick stream keeps it for
         * its next resume. The stream itself stays, with its subscription and
         * its open buckets.
         */
        sessions_.set_token(*handle, ev.token);
        market_feed_.retoken_or_open(ev);
        break;
    case event::event_kind::deployment_snapshot:
        sessions_.set_deployment(*handle, ev.deployment);
        BOOST_LOG_SEV(lg(), ores::logging::debug)
            << "Applied a deployment snapshot for '" << ev.instance_id
            << "' / '" << ev.account_id << "'";
        break;
    case event::event_kind::session_failed: {
        sessions_.set_status(*handle, domain::session_status::failed);
        market_feed_.close(ev.instance_id, ev.account_id);

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
    case event::event_kind::master_sealed:
    case event::event_kind::store_lock_failed:
    case event::event_kind::password_policy_fetched:
    case event::event_kind::series_catalogue:
    case event::event_kind::crm_names:
    case event::event_kind::crm_rates:
    case event::event_kind::term_structure:
    case event::event_kind::image_loaded:
    case event::event_kind::currency_image_map:
    case event::event_kind::series_points:
        /*
         * Never reached either, and for the same reason: each is applied above,
         * because none of them needs the session row.
         */
        break;
    }
}

std::expected<void, std::string> instance_manager::save_windows() {
    if (!window_service_.has_store())
        return std::unexpected(std::string("the store is not open"));

    if (auto refused = window_service_.save_windows()) {
        auto failure = refused->message;
        raise(std::move(refused->key), std::move(refused->message));
        return std::unexpected(std::move(failure));
    }
    conditions_.clear({condition_source::window_wall, {}});
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
