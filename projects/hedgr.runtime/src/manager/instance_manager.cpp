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
#include "hedgr.runtime/worker/crm_names_worker.hpp"
#include "hedgr.runtime/worker/crm_worker.hpp"
#include "hedgr.runtime/worker/deployment_worker.hpp"
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.runtime/worker/image_worker.hpp"
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.runtime/worker/term_structure_worker.hpp"
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
#include <limits>
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
using support::steady_now_ns;
using support::store_failure;

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
[[nodiscard]] std::string window_key_for(domain::window_kind kind,
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id = {}, std::string_view crm_name = {},
    std::string_view party_id = {}) {
    switch (kind) {
    case domain::window_kind::instance_manager:
        return "instance-manager";
    case domain::window_kind::accounts:
        return std::format("accounts:{}", instance_id);
    case domain::window_kind::workspace:
        return std::format("workspace:{}:{}", instance_id, account_id);
    case domain::window_kind::command_centre:
        return std::format("command-centre:{}:{}", instance_id, account_id);
    case domain::window_kind::series_browser:
        return std::format("series-browser:{}:{}", instance_id, account_id);
    case domain::window_kind::chart:
        /*
         * One chart per series per session. Reopening GBP/USD as the same
         * login focuses the plot already drawn; another login on the same
         * deployment is another session and gets its own window, with its own
         * identity bar and its own token.
         */
        return series_id.empty()
            ? std::string{}
            : std::format("chart:{}:{}:{}", instance_id, account_id, series_id);
    case domain::window_kind::term_structure:
        return series_id.empty()
            ? std::string{}
            : std::format("terms:{}:{}:{}", instance_id, account_id, series_id);
    case domain::window_kind::crm_matrix:
        return std::format("crm:{}:{}:{}:{}", instance_id, account_id, party_id,
            crm_name);
    case domain::window_kind::grid:
    case domain::window_kind::notice:
        break;
    }
    return {};
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

/**
 * @brief How often an open CRM matrix is read again.
 *
 * The deployment computes the batch on demand, so this is a request rate on a
 * party's rate engine, paid once per matrix however many windows show it.
 */
constexpr std::chrono::milliseconds crm_cadence{5000};

/**
 * @brief How many probes may be in flight at once.
 *
 * Two, from ADR 5: a slow probe cannot starve the other probes, and at most
 * two connects are in flight at once, which is a bound an operator can reason
 * about when twenty instances are unreachable.
 */
constexpr std::size_t probe_thread_count = 2;

/**
 * @brief The default kind-specific state for a window opened by kind.
 *
 * The instance and the account are parameters because two kinds cannot be
 * described without them: an accounts window is the accounts of one instance,
 * and a workspace is one account's session. Passing them in rather than
 * reaching for them keeps this function a pure mapping from a command to the
 * state its window starts with.
 */
[[nodiscard]] domain::window_state
default_state_for(domain::window_kind kind, std::string_view last_error,
    std::string_view instance_id, std::string_view account_id,
    std::string_view series_id = {},
    std::string_view series_label = {}) {
    switch (kind) {
    case domain::window_kind::chart: {
        domain::chart_state chart;
        chart.label = series_label.empty() ? "Series"
                                          : std::string{series_label};
        chart.series_id = std::string{series_id};
        return chart;
    }
    case domain::window_kind::series_browser:
        return domain::series_browser_state{};
    case domain::window_kind::crm_matrix:
        return domain::crm_matrix_state{};
    case domain::window_kind::term_structure: {
        domain::term_structure_state terms;
        terms.label = series_label.empty() ? "Term structure"
                                           : std::string{series_label};
        terms.curve_key = std::string{series_id};
        return terms;
    }
    case domain::window_kind::accounts: {
        domain::accounts_state accounts;
        accounts.instance_id = std::string{instance_id};
        return accounts;
    }
    case domain::window_kind::workspace: {
        domain::workspace_state session;
        session.account_id = std::string{account_id};
        return session;
    }
    case domain::window_kind::command_centre: {
        domain::command_centre_state centre;
        centre.instance_id = std::string{instance_id};
        return centre;
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
    for (auto& [key, entry] : market_streams_) {
        static_cast<void>(key);
        retire_market_stream(std::move(entry.stream));
    }
    market_streams_.clear();
    retired_market_streams_.clear();
    market_.clear();
    currency_images_.clear();
    currency_images_reading_.clear();
    images_.clear();
    images_reading_.clear();
    failed_images_.clear();
    term_structures_.clear();
    crm_.clear();
    catalogues_reading_.clear();
    points_reading_.clear();

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
        startup_failure_ = store_failure("cannot read the store's master password",
            has_master.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }
    store_lock_ = has_master.value() ? model::domain::store_lock::locked
                                     : model::domain::store_lock::absent;

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

    auto windows = window_repo_->list();
    if (!windows) {
        startup_failure_ = store_failure("cannot read the stored windows",
            windows.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
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

    for (auto& refused : seed::seed_store(seed_tables, seed_repositories,
             seed_options_))
        raise(std::move(refused.key), std::move(refused.message));
    ensure_instance_manager_window();

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
    probe_service_.pump_probes(started_, [this](const domain::instance& row) {
        return connection_options_for(row, seed_options_, database_->path());
    });
    pump_crm_reads();

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
    pump_market_streams();
}

std::string instance_manager::open_accounts_window(const std::string& instance_id) {
    /*
     * The same rule as every other window opened by the interface, reached
     * directly because this one is asked for by a caller that has no command
     * queue to push to: the id is derived from the instance, and one lookup
     * answers whether the window exists.
     */
    const std::string wanted = window_key_for(domain::window_kind::accounts,
        instance_id, {}, {});
    if (const auto existing = windows_.find(wanted)) {
        windows_.set_open(*existing, true);
        return wanted;
    }

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
        do_store_credential(cmd);
        break;
    case command::command_kind::load_image:
        do_load_image(cmd);
        break;
    case command::command_kind::load_term_structure:
        do_load_term_structure(cmd);
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
    case command::command_kind::configure_crm:
        do_configure_crm(cmd);
        break;
    case command::command_kind::load_crm_names:
        do_load_crm_names(cmd);
        break;
    case command::command_kind::probe_instance:
        if (auto refused = probe_service_.do_probe_instance(cmd))
            raise(std::move(refused->key), std::move(refused->message));
        break;
    case command::command_kind::probe_all:
        probe_service_.do_probe_all();
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
        for (const auto window : live_windows(windows_)) {
            if (windows_.session_of(window) == session)
                windows_.clear_session(window);
        }

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
        stop_market_stream(instance_id, id);
        for (const auto window : live_windows(windows_)) {
            if (windows_.session_of(window) == *session)
                windows_.clear_session(window);
        }
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
        if (cmd.remember && store_lock_ == domain::store_lock::unlocked) {
            start_credential_store(cmd.account_id,
                std::string{cmd.password.view()}, master_password_);
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
    if (store_lock_ != domain::store_lock::unlocked) {
        raise({condition_source::store_lock, cmd.account_id},
            std::format(
                "cannot connect to {}: the store is {}, so no password is "
                "available and none was supplied",
                instance_row->name, domain::to_string(store_lock_)));
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
    fact.lock = store_lock_;
    fact.message = std::move(message);
    events_.push(std::move(fact));
}

void instance_manager::do_store_credential(const command::command& cmd) {
    if (!account_repo_) {
        raise({condition_source::credential, cmd.account_id},
            "cannot store a password: the store is not open");
        return;
    }
    if (cmd.payload.empty()) {
        raise({condition_source::credential, cmd.account_id},
            "cannot store an empty credential");
        return;
    }

    if (auto saved = account_repo_->set_credential(cmd.account_id, cmd.payload);
        !saved) {
        raise({condition_source::credential, cmd.account_id},
            store_failure(
                std::format("cannot store the password for account {}",
                    cmd.account_id),
                saved.error()));
        return;
    }

    if (const auto handle = accounts_.find(cmd.account_id)) {
        auto row = *accounts_.get(*handle);
        row.has_credential = true;
        static_cast<void>(accounts_.upsert(row));
    }

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Stored a password for account '" << cmd.account_id << "'";
}

void instance_manager::start_credential_store(std::string account_id,
    std::string password, domain::secret_string master_password) {
    boost::asio::co_spawn(pool_.context(),
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

void instance_manager::do_set_master_password(const command::command& cmd) {
    if (!master_repo_) {
        raise({condition_source::store_lock, {}},
            "cannot set a master password: the store is not open");
        return;
    }

    /*
     * Refused when one already exists. Replacing it would be worse than
     * useless: the credentials in the store were sealed under the old password,
     * so the store would look healthy and open nothing.
     */
    auto existing = master_repo_->has_master();
    if (!existing) {
        raise({condition_source::store_lock, {}},
            store_failure("cannot read the store's master password",
                existing.error()));
        return;
    }
    if (existing.value()) {
        raise({condition_source::store_lock, {}},
            
                "this store already has a master password; unlock it instead");
        return;
    }

    if (cmd.password.empty()) {
        raise({condition_source::store_lock, {}},
            "the master password cannot be empty");
        return;
    }

    /*
     * The sealing is a derivation and therefore slow, so it happens on the pool
     * like every other slow call and reports through the same two events an
     * unlock does. The store ends up unlocked either way: the operator has just
     * chosen the password and holding it is what they asked for.
     */
    auto password = cmd.password;

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
        raise({condition_source::store_lock, {}},
            "cannot unlock: the store is not open");
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
        raise({condition_source::store_lock, {}},
            store_failure("cannot read the master password",
                blob.error()));
        return;
    }
    if (blob.value().empty()) {
        store_lock_ = domain::store_lock::absent;
        lock_error_ = "this store has no master password yet";
        return;
    }

    auto password = cmd.password;
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
    stop_market_stream(cmd.instance_id, cmd.account_id);

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
    static_cast<void>(save_windows());
}

void instance_manager::do_open_window(const command::command& cmd) {
    if (!window_repo_) {
        raise({condition_source::window, {}},
            "cannot open a window: the store is not open");
        return;
    }

    /*
     * A window whose id is derived from what it shows is opened by focusing the
     * one that already exists. That is what makes two requests for the same
     * subject one window, and it is the reason the ids of those kinds are
     * functions rather than serials.
     */
    const auto key = window_key_for(cmd.new_window_kind, cmd.instance_id,
        cmd.account_id, cmd.series_id, cmd.crm_name, cmd.party_id);
    if (!key.empty()) {
        if (const auto existing = windows_.find(key)) {
            windows_.set_open(*existing, true);
            windows_.request_focus(key);

            /*
             * A window restored from the store is open and unbound: the
             * session it was saved against does not exist at start. A sign-in
             * that asks for that window again is the moment the binding can be
             * made, and without it the window draws the session-loss state
             * forever while its session is live beside it.
             */
            if (const auto session =
                    sessions_.find(cmd.instance_id, cmd.account_id)) {
                windows_.bind_session(*existing, *session);

                /*
                 * The restored window's first refresh ran while it had no
                 * session, so its deadline has already moved a cadence out.
                 * Binding makes the read possible, and asking for it now is
                 * what makes the window show the deployment rather than sit on
                 * "reading" until the next tick.
                 */
                windows_.set_next_refresh_ns(*existing,
                    cmd.new_window_kind == domain::window_kind::chart
                        || cmd.new_window_kind
                            == domain::window_kind::term_structure
                        ? std::numeric_limits<std::int64_t>::max()
                        : 0);
                if (cmd.new_window_kind
                    == domain::window_kind::term_structure)
                    start_term_structure_read(*existing);
            }
            return;
        }
    }

    domain::window_spec spec;
    spec.id = key.empty() ? std::format("w-{}", next_window_serial_++)
                          : key;
    spec.kind = cmd.new_window_kind;

    /*
     * A command centre reads a deployment, not a frame: six list calls on a
     * second connection. The one second default is the placeholder's cadence
     * and would put six requests a second on the broker for a screen whose
     * numbers move over hours.
     */
    if (cmd.new_window_kind == domain::window_kind::command_centre)
        spec.cadence = std::chrono::seconds{30};
    if (cmd.new_window_kind == domain::window_kind::series_browser)
        spec.cadence = std::chrono::seconds{60};
    if (cmd.new_window_kind == domain::window_kind::chart)
        spec.cadence = std::chrono::seconds{2};
    if (cmd.new_window_kind == domain::window_kind::term_structure)
        spec.cadence = std::chrono::seconds{30};

    std::string_view last_error;
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        last_error = sessions_.last_error(*session);
    spec.state = default_state_for(cmd.new_window_kind, last_error,
        cmd.instance_id, cmd.account_id, cmd.series_id, cmd.series_label);
    if (cmd.new_window_kind == domain::window_kind::crm_matrix) {
        auto& crm = std::get<domain::crm_matrix_state>(spec.state);
        crm.crm_name = cmd.crm_name;
        if (!cmd.party_id.empty()) {
            crm.party_id = cmd.party_id;
        } else if (const auto session = sessions_.find(cmd.instance_id,
                       cmd.account_id)) {
            crm.party_id = std::string{sessions_.party_id(*session)};
        }
    }
    if (cmd.new_window_kind == domain::window_kind::term_structure) {
        auto& terms = std::get<domain::term_structure_state>(spec.state);
        terms.member_ids = cmd.member_ids;
        terms.member_tenors = cmd.member_tenors;
    }

    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        spec.session = domain::session_key{std::string{cmd.instance_id},
            std::string{cmd.account_id}};

    const auto handle = windows_.insert(spec);

    /*
     * A window opened from a session is bound to it, so the frame loop can
     * reach its state without a key lookup per window. A window opened with no
     * session named stays unbound and renders the session-loss state.
     */
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id))
        windows_.bind_session(handle, *session);

    /*
     * A chart is fed by the subscription, so its first backfill is asked for by
     * the watch rather than by the refresh deadline. The deadline is parked at
     * the end of time; a manual refresh still starts a read.
     */
    if (cmd.new_window_kind == domain::window_kind::chart
        || cmd.new_window_kind == domain::window_kind::term_structure
        || cmd.new_window_kind == domain::window_kind::crm_matrix) {
        windows_.set_next_refresh_ns(handle,
            std::numeric_limits<std::int64_t>::max());
    }

    if (cmd.new_window_kind == domain::window_kind::term_structure)
        start_term_structure_read(handle);

    if (auto saved = window_repo_->save(windows_.spec(handle, sessions_)); !saved) {
        raise({condition_source::window, spec.id},
            store_failure(
                std::format("cannot save window {}", spec.id), saved.error()));
    }

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Opened window '" << spec.id << "' of kind '"
        << domain::to_string(spec.kind) << "'";
}

void instance_manager::do_close_window(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Closing window '" << cmd.window_id << "'";
    const auto handle = windows_.find(cmd.window_id);
    if (!handle) {
        raise({condition_source::window, cmd.window_id},
            std::format("cannot close: no window {}", cmd.window_id));
        return;
    }

    windows_.remove(*handle);

    if (window_repo_) {
        if (auto removed = window_repo_->remove(cmd.window_id); !removed) {
            raise({condition_source::window, cmd.window_id},
                store_failure(
                    std::format("cannot remove window {}", cmd.window_id),
                    removed.error()));
        }
    }
}

void instance_manager::do_load_crm_names(const command::command& cmd) {
    start_crm_names_read(cmd.instance_id, cmd.account_id, cmd.party_id);
}

void instance_manager::do_configure_crm(const command::command& cmd) {
    const auto handle = windows_.find(cmd.window_id);
    if (!handle)
        return;
    auto* crm = std::get_if<domain::crm_matrix_state>(
        &windows_.state(*handle));
    if (crm == nullptr)
        return;
    crm->party_id = cmd.party_id;
    crm->crm_name = cmd.crm_name;
    crm->reciprocal = cmd.crm_reciprocal;
    static_cast<void>(save_windows());
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

    /*
     * A chart has no cadence any more. Its prices arrive on the session's
     * subscription; a refresh is the operator asking for the backfill again,
     * and it must not rearm a poll. A CRM matrix's cadence belongs to its
     * batch, which pump_crm_reads keeps.
     */
    windows_.set_next_refresh_ns(*handle,
        spec.kind == domain::window_kind::chart
                || spec.kind == domain::window_kind::crm_matrix
            ? std::numeric_limits<std::int64_t>::max()
            : due.count());

    /*
     * A refresh is also the operator's chance to retry the currency image map
     * when its first read failed; flags fall back to the drawn ones until it
     * answers.
     */
    if (const auto session = sessions_.find(cmd.instance_id, cmd.account_id);
        session && sessions_.status(*session)
            == domain::session_status::authenticated) {
        const auto tenant = std::string{sessions_.tenant_id(*session)};
        const auto* map = currency_images_.get(tenant);
        if (map == nullptr
            || map->state == model::state::image_map_state::failed) {
            model::event::event retry;
            retry.instance_id = cmd.instance_id;
            retry.account_id = cmd.account_id;
            retry.tenant_id = tenant;
            start_currency_image_map(retry);
        }
    }

    /*
     * A command centre's refresh is a deployment read on its own connection.
     * Every other kind still takes the placeholder below.
     */
    if (spec.kind == domain::window_kind::term_structure) {
        start_term_structure_read(*handle);
        return;
    }
    if (spec.kind == domain::window_kind::crm_matrix) {
        start_crm_read(*handle);
        return;
    }

    if (spec.kind == domain::window_kind::command_centre) {
        start_deployment_read(cmd.window_id);
        return;
    }

    if (spec.kind == domain::window_kind::series_browser) {
        start_market_catalogue_read(cmd.window_id);
        return;
    }

    if (spec.kind == domain::window_kind::chart) {
        if (const auto* chart = std::get_if<domain::chart_state>(
                &windows_.state(*handle));
            chart != nullptr && !chart->series_id.empty()) {
            start_market_points_read(cmd.window_id, chart->series_id);
        }
        return;
    }

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

void instance_manager::rebind_restored_windows(std::string_view instance_id,
    std::string_view account_id, model_state::session_handle session) {
    const auto& layout = windows_.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;

        const model_state::window_handle window{
            index, layout.generation[index]};
        if (windows_.session_of(window))
            continue;

        const auto spec = windows_.spec(window, sessions_);
        if (!spec.session)
            continue;
        if (spec.session->instance_id != instance_id
            || spec.session->account_id != account_id) {
            continue;
        }

        windows_.bind_session(window, session);
        windows_.set_next_refresh_ns(window,
            spec.kind == domain::window_kind::chart
                ? std::numeric_limits<std::int64_t>::max()
                : 0);
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Rebound restored window '" << spec.id << "' to '"
            << instance_id << "' / '" << account_id << "'";
    }
}

void instance_manager::start_deployment_read(std::string_view window_id) {
    if (!database_) {
        raise({condition_source::read, std::string{window_id}},
            "cannot read the deployment: the store is not open");
        return;
    }

    const auto window = windows_.find(window_id);
    if (!window)
        return;

    const auto session = windows_.session_of(*window);
    if (!session)
        return;

    /*
     * Only a signed-in session has a token to present, and a window that
     * outlived its session has nothing to read with. The window draws its
     * session-loss state instead.
     */
    if (sessions_.status(*session)
        != domain::session_status::authenticated) {
        return;
    }

    const auto token = std::string{sessions_.token(*session)};
    if (token.empty())
        return;

    const auto instance_id = std::string{sessions_.instance_id(*session)};
    const auto account_id = std::string{sessions_.account_id(*session)};

    const auto instance = instances_.find(instance_id);
    if (!instance)
        return;

    const auto key = running_key(instance_id, account_id);
    if (deployment_reads_.contains(key))
        return;

    const auto account = accounts_.find(account_id);
    const std::string principal = account
        ? accounts_.get(*account)->principal : std::string{};

    worker::deployment_job job;
    job.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.key = domain::session_key{instance_id, account_id};
    job.token = token;
    job.principal = principal;

    deployment_reads_.insert(key);
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a deployment read for '" << instance_id << "' / '"
        << account_id << "'";
    worker::spawn_deployment_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_market_catalogue_read(
    std::string_view window_id) {
    start_market_read(window_id, {});
}

void instance_manager::start_market_points_read(std::string_view window_id,
    std::string_view series_id) {
    start_market_read(window_id, series_id);
}

void instance_manager::start_market_read(std::string_view window_id,
    std::string_view series_id) {
    if (!database_) {
        raise({condition_source::read, std::string{window_id}},
            "cannot read the market: the store is not open");
        return;
    }

    const auto window = windows_.find(window_id);
    if (!window)
        return;

    const auto session = windows_.session_of(*window);
    if (!session)
        return;

    if (sessions_.status(*session) != domain::session_status::authenticated)
        return;

    const auto token = std::string{sessions_.token(*session)};
    if (token.empty())
        return;

    const auto instance_id = std::string{sessions_.instance_id(*session)};
    const auto account_id = std::string{sessions_.account_id(*session)};

    const auto instance = instances_.find(instance_id);
    if (!instance)
        return;

    const bool catalogue = series_id.empty();
    const auto session_key = running_key(instance_id, account_id);
    const auto points_key =
        std::format("{}\x1f{}", session_key, series_id);
    if (catalogue) {
        if (catalogues_reading_.contains(session_key))
            return;
    } else if (points_reading_.contains(points_key)) {
        return;
    }

    const auto account = accounts_.find(account_id);
    const std::string principal = account
        ? accounts_.get(*account)->principal : std::string{};

    worker::market_job job;
    job.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.key = domain::session_key{instance_id, account_id};
    job.token = token;
    job.principal = principal;
    job.catalogue = catalogue;
    job.series_id = std::string{series_id};

    if (catalogue) {
        catalogues_reading_.insert(session_key);
        market_.begin_catalogue(instance_id, account_id);
    } else {
        points_reading_.insert(points_key);
    }

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a market " << (catalogue ? "catalogue" : "points")
        << " read for '" << instance_id << "' / '" << account_id << "'"
        << (catalogue ? std::string{}
                      : std::format(" series '{}'", series_id));
    worker::spawn_market_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_currency_image_map(const model::event::event& ev) {
    if (ev.tenant_id.empty() || !database_)
        return;
    if (const auto* existing = currency_images_.get(ev.tenant_id);
        existing != nullptr
        && (existing->state == model::state::image_map_state::ready
            || existing->state == model::state::image_map_state::reading))
        return;
    if (currency_images_reading_.contains(ev.tenant_id))
        return;
    const auto session = sessions_.find(ev.instance_id, ev.account_id);
    if (!session)
        return;
    const auto instance = instances_.find(ev.instance_id);
    if (!instance)
        return;
    const auto token = std::string{sessions_.token(*session)};
    if (token.empty())
        return;

    const auto account = accounts_.find(ev.account_id);
    worker::image_map_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = ev.instance_id;
    job.session.account_id = ev.account_id;
    job.session.tenant_id = ev.tenant_id;
    job.session.token = token;
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    currency_images_reading_.insert(ev.tenant_id);
    currency_images_.begin(ev.tenant_id);
    worker::spawn_image_map_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_crm_names_read(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    if (!database_)
        return;
    const auto session = sessions_.find(instance_id, account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;
    const auto instance = instances_.find(instance_id);
    if (!instance)
        return;
    const auto account = accounts_.find(account_id);
    worker::crm_names_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = std::string{instance_id};
    job.session.account_id = std::string{account_id};
    job.session.tenant_id = std::string{sessions_.tenant_id(*session)};
    job.session.token = std::string{sessions_.token(*session)};
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    job.party_id = std::string{party_id};
    worker::spawn_crm_names_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_crm_read(
    model::state::window_handle handle) {
    if (!database_ || !windows_.contains(handle))
        return;
    const auto spec = windows_.spec(handle, sessions_);
    const auto* state = std::get_if<domain::crm_matrix_state>(&spec.state);
    if (state == nullptr || !spec.session || state->party_id.empty())
        return;
    const auto session = sessions_.find(spec.session->instance_id,
        spec.session->account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;
    const auto instance = instances_.find(spec.session->instance_id);
    if (!instance)
        return;
    const auto account = accounts_.find(spec.session->account_id);
    worker::crm_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = spec.session->instance_id;
    job.session.account_id = spec.session->account_id;
    job.session.tenant_id = std::string{sessions_.tenant_id(*session)};
    job.session.token = std::string{sessions_.token(*session)};
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    job.party_id = state->party_id;
    job.crm_name = state->crm_name;
    job.reciprocal = state->reciprocal;
    if (const auto* current = crm_.get(spec.session->instance_id,
            spec.session->account_id, state->party_id, state->crm_name,
            state->reciprocal);
        current != nullptr && current->reading)
        return;
    crm_.begin(spec.session->instance_id, spec.session->account_id,
        state->party_id, state->crm_name, state->reciprocal);
    worker::spawn_crm_read(pool_.context(), std::move(job), events_);
}

void instance_manager::pump_crm_reads() {
    if (!database_)
        return;
    const auto now = steady_now_ns();
    const auto& layout = windows_.layout();
    const auto kinds = windows_.kind_column();
    const auto open = windows_.open_column();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0 || open[index] == 0
            || static_cast<domain::window_kind>(kinds[index])
                != domain::window_kind::crm_matrix)
            continue;
        const model_state::window_handle window{index,
            layout.generation[index]};
        const auto session = windows_.session_of(window);
        if (!session || sessions_.status(*session)
                != domain::session_status::authenticated)
            continue;
        const auto* crm =
            std::get_if<domain::crm_matrix_state>(&windows_.state(window));
        if (crm == nullptr || crm->party_id.empty() || crm->crm_name.empty())
            continue;
        const auto* batch = crm_.get(sessions_.instance_id(*session),
            sessions_.account_id(*session), crm->party_id, crm->crm_name,
            crm->reciprocal);
        if (batch != nullptr && (batch->reading || now < batch->next_read_ns))
            continue;
        start_crm_read(window);
    }
}

void instance_manager::start_term_structure_read(
    model::state::window_handle handle) {
    if (!database_ || !windows_.contains(handle))
        return;
    const auto spec = windows_.spec(handle, sessions_);
    const auto* st = std::get_if<domain::term_structure_state>(&spec.state);
    if (st == nullptr || st->curve_key.empty() || st->member_ids.empty()
        || st->member_ids.size() != st->member_tenors.size())
        return;
    if (!spec.session)
        return;
    const auto session = sessions_.find(spec.session->instance_id,
        spec.session->account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;
    const auto instance = instances_.find(spec.session->instance_id);
    if (!instance)
        return;
    const auto account = accounts_.find(spec.session->account_id);

    worker::term_structure_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = spec.session->instance_id;
    job.session.account_id = spec.session->account_id;
    job.session.tenant_id = std::string{sessions_.tenant_id(*session)};
    job.session.token = std::string{sessions_.token(*session)};
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    job.key = st->curve_key;
    job.label = st->label;
    job.member_ids = st->member_ids;
    job.member_tenors = st->member_tenors;
    term_structures_.begin(spec.session->instance_id,
        spec.session->account_id, st->curve_key, st->label);
    worker::spawn_term_structure_read(pool_.context(), std::move(job), events_);
}

void instance_manager::do_load_term_structure(
    const model::command::command& cmd) {
    if (!database_ || cmd.curve_key.empty()
        || cmd.member_ids.size() != cmd.member_tenors.size()
        || cmd.member_ids.empty())
        return;
    const auto session = sessions_.find(cmd.instance_id, cmd.account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;
    const auto instance = instances_.find(cmd.instance_id);
    if (!instance)
        return;
    const auto account = accounts_.find(cmd.account_id);
    const auto token = std::string{sessions_.token(*session)};

    worker::term_structure_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = cmd.instance_id;
    job.session.account_id = cmd.account_id;
    job.session.tenant_id = std::string{sessions_.tenant_id(*session)};
    job.session.token = token;
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    job.key = cmd.curve_key;
    job.label = cmd.curve_label;
    job.member_ids = cmd.member_ids;
    job.member_tenors = cmd.member_tenors;
    term_structures_.begin(cmd.instance_id, cmd.account_id, cmd.curve_key,
        cmd.curve_label);
    worker::spawn_term_structure_read(pool_.context(), std::move(job), events_);
}

void instance_manager::do_load_image(const model::command::command& cmd) {
    if (!database_)
        return;
    const auto session = sessions_.find(cmd.instance_id, cmd.account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;
    const auto tenant = std::string{sessions_.tenant_id(*session)};
    const auto token = std::string{sessions_.token(*session)};
    if (tenant.empty() || token.empty() || cmd.image_id.empty())
        return;
    const auto key = model::state::make_tenant_key(tenant, cmd.image_id);
    if (images_reading_.contains(key) || images_.contains(tenant, cmd.image_id)
        || failed_images_.contains(key))
        return;
    const auto instance = instances_.find(cmd.instance_id);
    if (!instance)
        return;
    const auto account = accounts_.find(cmd.account_id);

    worker::image_job job;
    job.session.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.session.instance_id = cmd.instance_id;
    job.session.account_id = cmd.account_id;
    job.session.tenant_id = tenant;
    job.session.token = token;
    job.session.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    job.image_id = cmd.image_id;
    images_reading_.insert(key);
    worker::spawn_image_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_market_backfill(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    if (!database_)
        return;

    const auto session = sessions_.find(instance_id, account_id);
    if (!session || sessions_.status(*session)
            != domain::session_status::authenticated)
        return;

    const auto token = std::string{sessions_.token(*session)};
    if (token.empty())
        return;

    const auto instance = instances_.find(instance_id);
    if (!instance)
        return;

    const auto session_key =
        running_key(instance_id, account_id);
    const auto points_key = std::format("{}\x1f{}", session_key, series_id);
    if (points_reading_.contains(points_key))
        return;

    const auto account = accounts_.find(account_id);
    const std::string principal = account
        ? accounts_.get(*account)->principal : std::string{};

    worker::market_job job;
    job.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    job.key = domain::session_key{std::string{instance_id},
        std::string{account_id}};
    job.token = token;
    job.principal = principal;
    job.catalogue = false;
    job.series_id = std::string{series_id};
    job.limit = 2000;

    points_reading_.insert(points_key);
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Seeding live history for series '" << series_id << "' on '"
        << instance_id << "' / '" << account_id << "'";
    worker::spawn_market_read(pool_.context(), std::move(job), events_);
}

void instance_manager::start_market_stream(const model::event::event& ev) {
    stop_market_stream(ev.instance_id, ev.account_id);
    if (!database_)
        return;
    const auto instance = instances_.find(ev.instance_id);
    if (!instance)
        return;

    worker::market_stream_options options;
    options.options = connection_options_for(*instances_.get(*instance),
        seed_options_, database_->path());
    options.token = std::string{ev.token.view()};
    options.principal = ev.payload;
    options.tenant_id = ev.tenant_id;
    options.party_id = ev.party_id;

    auto stream = std::make_unique<worker::market_stream>(std::move(options));
    stream->start();

    market_stream_entry entry;
    entry.instance_id = ev.instance_id;
    entry.account_id = ev.account_id;
    entry.stream = std::move(stream);
    market_streams_.insert_or_assign(
        running_key(ev.instance_id, ev.account_id), std::move(entry));
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Market tick stream opened for '" << ev.instance_id << "' / '"
        << ev.account_id << "' in party '" << ev.party_id << "'";
}

void instance_manager::stop_market_stream(std::string_view instance_id,
    std::string_view account_id) {
    const auto key = running_key(instance_id, account_id);
    const auto found = market_streams_.find(key);
    if (found == market_streams_.end())
        return;
    retire_market_stream(std::move(found->second.stream));
    market_streams_.erase(found);
}

void instance_manager::retire_market_stream(
    std::unique_ptr<worker::market_stream> stream) {
    if (!stream)
        return;
    stream->request_stop();
    retired_market_streams_.push_back(std::move(stream));
}

void instance_manager::pump_market_streams() {
    /*
     * What the open charts need, by session. A watch is only possible once the
     * catalogue names the series' URI, so a chart opened before the catalogue
     * read is watched on a later frame.
     */
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>> wanted;

    for (const auto window : live_windows(windows_)) {
        if (windows_.kind(window) != domain::window_kind::chart)
            continue;
        const auto session = windows_.session_of(window);
        if (!session)
            continue;
        const auto* chart =
            std::get_if<domain::chart_state>(&windows_.state(window));
        if (chart == nullptr || chart->series_id.empty())
            continue;

        const auto instance_id = sessions_.instance_id(*session);
        const auto account_id = sessions_.account_id(*session);
        const auto* catalogue = market_.catalogue(instance_id, account_id);
        if (catalogue == nullptr
            || catalogue->state
                == model_state::catalogue_state::failed) {
            /*
             * A restored chart has a series id and no catalogue to turn it into
             * the URI a subscription needs. Reading the catalogue is what the
             * series browser does; doing it here as well is what lets a chart
             * that outlived the process come back to life without one. A
             * failed read is left to the browser's cadence rather than retried
             * on every frame.
             */
            if (catalogue == nullptr)
                start_market_catalogue_read(windows_.id(window));
            continue;
        }

        const auto row = std::find_if(catalogue->series.begin(),
            catalogue->series.end(), [&chart](const auto& series) {
                return series.id == chart->series_id;
            });
        if (row == catalogue->series.end())
            continue;

        /*
         * The tick subject is party scoped and the token is for the session's
         * party. A chart looking at another party through the picker keeps its
         * backfill and manual refresh, not a live stream.
         */
        if (row->party_id != sessions_.party_id(*session))
            continue;

        wanted[running_key(instance_id, account_id)]
            .emplace_back(row->id, row->uri);
    }

    std::vector<model::state::market_bucket> buckets;
    const auto now = steady_now_ns();

    for (auto it = market_streams_.begin(); it != market_streams_.end();) {
        auto& entry = it->second;
        const auto session = sessions_.find(entry.instance_id,
            entry.account_id);
        if (!session
            || sessions_.status(*session)
                != domain::session_status::authenticated) {
            retire_market_stream(std::move(entry.stream));
            it = market_streams_.erase(it);
            continue;
        }

        const auto needed = wanted.find(it->first);
        std::unordered_map<std::string, std::string> desired;
        if (needed != wanted.end()) {
            for (const auto& [series_id, uri] : needed->second)
                desired.emplace(series_id, uri);
        }

        for (const auto& [series_id, uri] : desired) {
            if (entry.watched.contains(series_id))
                continue;
            entry.stream->watch(series_id, uri);
            entry.watched.emplace(series_id, uri);
            start_market_backfill(entry.instance_id, entry.account_id,
                series_id);
        }
        for (auto watch = entry.watched.begin(); watch != entry.watched.end();) {
            if (desired.contains(watch->first)) {
                ++watch;
                continue;
            }
            entry.stream->unwatch(watch->first);
            watch = entry.watched.erase(watch);
        }

        if (entry.stream->take_reconnected()) {
            for (const auto& [series_id, uri] : entry.watched) {
                static_cast<void>(uri);
                start_market_backfill(entry.instance_id, entry.account_id,
                    series_id);
            }
        }

        entry.stream->drain(buckets);
        for (const auto& bucket : buckets) {
            market_.append_point(entry.instance_id, entry.account_id,
                bucket.series_id, bucket.level, bucket.point, now);
        }

        ++it;
    }

    std::erase_if(retired_market_streams_,
        [](const auto& stream) { return stream->finished(); });
}

bool instance_manager::apply_read_answer(const event::event& ev) {
    switch (ev.kind) {
    case event::event_kind::series_catalogue: {
        catalogues_reading_.erase(
            running_key(ev.instance_id, ev.account_id));
        if (ev.message.empty()) {
            market_.set_catalogue(ev.instance_id, ev.account_id,
                ev.series);
        } else {
            market_.fail_catalogue(ev.instance_id, ev.account_id,
                ev.message);
        }
        return true;
    }
    case event::event_kind::crm_names: {
        crm_.set_names(ev.instance_id, ev.account_id, ev.party_id,
            ev.crm_names);
        return true;
    }
    case event::event_kind::crm_rates: {
        if (ev.message.empty())
            crm_.set(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
                ev.crm_reciprocal, ev.crm_cells, ev.crm_conventions,
                ev.crm_as_of_ns, ev.crm_latency_ns);
        else
            crm_.fail(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
                ev.crm_reciprocal, ev.message);
        crm_.schedule(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
            ev.crm_reciprocal, steady_now_ns()
                + std::chrono::duration_cast<std::chrono::nanoseconds>(
                    crm_cadence).count());
        return true;
    }
    case event::event_kind::term_structure: {
        if (ev.message.empty()) {
            term_structures_.set(ev.instance_id, ev.account_id, ev.curve_key,
                ev.curve_label, ev.term_curves);
        } else {
            term_structures_.fail(ev.instance_id, ev.account_id, ev.curve_key,
                ev.message);
        }
        return true;
    }
    case event::event_kind::image_loaded: {
        const auto key = model::state::make_tenant_key(ev.tenant_id,
            ev.image_id);
        images_reading_.erase(key);
        if (ev.message.empty()) {
            if (images_.put(ev.tenant_id, ev.image_id,
                    model::state::image_content{.mime_type = ev.mime_type,
                        .data = ev.bytes})) {
                failed_images_.erase(key);
            } else {
                failed_images_.insert(key);
            }
        } else {
            failed_images_.insert(key);
        }
        return true;
    }
    case event::event_kind::currency_image_map: {
        currency_images_reading_.erase(ev.tenant_id);
        if (ev.message.empty()) {
            std::vector<std::pair<std::string, std::string>> mapped;
            for (const auto& image : ev.image_refs) {
                mapped.emplace_back(image.currency, image.image_id);
            }
            currency_images_.set(ev.tenant_id, std::move(mapped));
        } else {
            currency_images_.fail(ev.tenant_id, ev.message);
        }
        return true;
    }
    case event::event_kind::series_points: {
        const auto key = std::format("{}\x1f{}",
            running_key(ev.instance_id, ev.account_id), ev.series_id);
        points_reading_.erase(key);
        if (ev.message.empty()) {
            market_.set_points(ev.instance_id, ev.account_id, ev.series_id,
                ev.points, ev.observed_at_ns);
        } else {
            market_.fail_points(ev.instance_id, ev.account_id, ev.series_id,
                ev.message, ev.observed_at_ns);
        }
        return true;
    }
    case event::event_kind::deployment_snapshot:
        /* The snapshot itself belongs on the session row, applied below. */
        deployment_reads_.erase(running_key(ev.instance_id, ev.account_id));
        return false;
    default:
        return false;
    }
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
        conditions_.clear(condition_source::store_lock);
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

    /*
     * A read's answer is applied before the session lookup. The caches it
     * fills are keyed by name rather than by the session row, and the answer
     * is the only thing that clears the read's in-flight mark: dropping it
     * because the operator signed out meanwhile would leave the read marked as
     * running, and refuse it again after the next sign-in.
     */
    if (apply_read_answer(ev))
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
        start_market_stream(ev);
        start_currency_image_map(ev);

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
        {
            command::command open;
            open.kind = command::command_kind::open_window;
            open.instance_id = ev.instance_id;
            open.account_id = ev.account_id;
            open.new_window_kind =
                sessions_.mode(*handle) == domain::session_mode::system_administration
                ? domain::window_kind::command_centre
                : domain::window_kind::workspace;
            do_open_window(open);
        }

        /*
         * Every other window the wall restored was saved against this same
         * pair and is still unbound. Binding them here is what makes a
         * restored series browser or chart live on the sign-in that owns it.
         */
        rebind_restored_windows(ev.instance_id, ev.account_id, *handle);
        break;
    case event::event_kind::session_token_refreshed:
        /*
         * A refresh replaces the credential, not the session. Reads that start
         * from here on take the new token, and the tick stream keeps it for
         * its next resume. The stream itself stays, with its subscription and
         * its open buckets.
         */
        sessions_.set_token(*handle, ev.token);
        if (const auto stream = market_streams_.find(
                running_key(ev.instance_id, ev.account_id));
            stream != market_streams_.end() && stream->second.stream) {
            stream->second.stream->set_token(std::string{ev.token.view()});
        } else {
            start_market_stream(ev);
        }
        break;
    case event::event_kind::deployment_snapshot:
        sessions_.set_deployment(*handle, ev.deployment);
        BOOST_LOG_SEV(lg(), ores::logging::debug)
            << "Applied a deployment snapshot for '" << ev.instance_id
            << "' / '" << ev.account_id << "'";
        break;
    case event::event_kind::session_failed: {
        sessions_.set_status(*handle, domain::session_status::failed);
        stop_market_stream(ev.instance_id, ev.account_id);

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
    if (!window_repo_)
        return std::unexpected(std::string("the store is not open"));

    for (const auto& spec : windows_.specs(sessions_)) {
        if (auto saved = window_repo_->save(spec); !saved) {
            auto failure = store_failure(
                std::format("cannot save window {}", spec.id), saved.error());
            raise({condition_source::window_wall, {}}, failure);
            return std::unexpected(std::move(failure));
        }
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
