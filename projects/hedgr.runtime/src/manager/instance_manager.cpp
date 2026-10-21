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
    , market_feed_(catalogue_.instances(), sessions_, series_, seed_options_,
          std::move(market_feeds),
          [this](std::string_view instance_id, std::string_view account_id) {
              return read_service_.start_market_catalogue_read(instance_id,
                  account_id);
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

    catalogue_.attach(*database_);
    window_service_.attach(*database_);
    lock_service_.attach(*database_, catalogue_.account_repository());
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

    if (auto loaded = catalogue_.load(); !loaded) {
        startup_failure_ = std::move(loaded.error());
        raise({condition_source::startup, {}}, startup_failure_);
        return std::unexpected(startup_failure_);
    }

    /*
     * Immediately after the read and before anything that could depend on it,
     * so a store written by a build that lost the keys has them back for this
     * process, not the next one. It is a start step rather than a migration
     * because it has to reach every store, including ones already stamped 8,
     * and because a migration runs once where this runs every time.
     */
    const auto seed_tables = catalogue_.seed_tables();
    const auto seed_repositories = catalogue_.seed_repositories();
    for (auto& refused :
        seed::backfill_seeded_icon_keys(seed_tables, seed_repositories))
        raise(std::move(refused.key), std::move(refused.message));

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
        << "Runtime ready with " << catalogue_.instances().size() << " instances and "
        << catalogue_.accounts().size() << " accounts";
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
    probe_service_.pump_probes(started_,
        [this](const domain::instance& row) {
            return connection_options_for(row, seed_options_, database_->path());
        },
        [this](const domain::instance& row) {
            return wire_format_holder(row) == nullptr;
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
    case command::command_kind::remove_instance:
    case command::command_kind::save_account:
    case command::command_kind::remove_account:
    case command::command_kind::save_folder:
    case command::command_kind::remove_folder:
    case command::command_kind::save_label:
    case command::command_kind::remove_label:
    case command::command_kind::attach_label:
    case command::command_kind::detach_label:
    case command::command_kind::save_shortcut:
    case command::command_kind::remove_shortcut:
        complete_catalogue(catalogue_.apply(cmd));
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
    case command::command_kind::load_history:
        read_service_.load_history(cmd);
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
    const auto instance = catalogue_.instances().find(cmd.instance_id);
    if (!instance) {
        raise({condition_source::session, cmd.account_id},
            std::format("cannot connect: no instance {}",
                cmd.instance_id));
        return;
    }
    const auto* instance_row = catalogue_.instances().get(*instance);

    if (!catalogue_.accounts().find(cmd.account_id)) {
        raise({condition_source::session, cmd.account_id},
            std::format("cannot connect: no account {}",
                cmd.account_id));
        return;
    }

    /*
     * Before the password is remembered or a derivation is started: a refusal
     * starts nothing, and an operator who is told to disconnect another
     * session first should not also find a credential saved for an attempt
     * that never ran.
     */
    if (const auto* holder = wire_format_holder(*instance_row)) {
        raise({condition_source::session, cmd.account_id},
            std::format(
                "cannot connect to {}: it uses the {} wire format, but {} is "
                "signed in using {}, and this process speaks one wire format at "
                "a time; disconnect {} first",
                instance_row->name, instance_row->wire_format, holder->name,
                holder->wire_format, holder->name));
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

    auto sealed = catalogue_.account_repository().credential_of(cmd.account_id);
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

const domain::instance* instance_manager::wire_format_holder(
    const domain::instance& row) const {
    for (const auto session : live_sessions(sessions_)) {
        const auto status = sessions_.status(session);
        if (status != domain::session_status::connecting
            && status != domain::session_status::authenticated)
            continue;
        const auto instance_id = sessions_.instance_id(session);
        const auto held = catalogue_.instances().find(instance_id);
        if (!held)
            continue;
        const auto* other = catalogue_.instances().get(*held);
        if (other->wire_format != row.wire_format)
            return other;
    }
    return nullptr;
}

void instance_manager::start_session_for(const command::command& cmd,
    domain::credential credential, std::string sealed_credential) {
    const auto instance = catalogue_.instances().find(cmd.instance_id);
    if (!instance)
        return;
    const auto* instance_row = catalogue_.instances().get(*instance);

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

void instance_manager::complete_catalogue(service::catalogue_effect effect) {
    for (auto& refused : effect.refusals)
        raise(std::move(refused.key), std::move(refused.message));
    if (!effect.removed)
        return;

    const auto& gone = *effect.removed;
    if (gone.probe) {
        /*
         * Everything the schedule holds for this instance goes with it, and
         * the probe already in flight is cancelled: a result for a removed
         * instance would be applied to whatever takes its slot next, and the
         * deadline would keep a probe running for a row nobody can read.
         */
        probe_service_.forget_probe(*gone.probe, gone.instance_id);
    }

    if (gone.account_id.empty()) {
        /*
         * A session belongs to the pair, so removing the instance removes its
         * sessions: the worker is asked to stop before the row goes, and any event
         * it still manages to send arrives for a session that is no longer there
         * and is ignored.
         */
        for (const auto session : live_sessions(sessions_)) {
            if (sessions_.instance_id(session) != gone.instance_id)
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
    } else if (const auto session =
                   sessions_.find(gone.instance_id, gone.account_id)) {
        /*
         * One account is at most one session, because a session is keyed by the
         * pair, so the row to stop is looked up by the pair rather than walked
         * for.
         */
        stop_session(running_key(gone.instance_id, gone.account_id));
        market_feed_.close(gone.instance_id, gone.account_id);
        window_service_.unbind(*session);
        sessions_.remove(*session);
    }

    /*
     * The windows give back what they hold before the rows go, because a row
     * is read through its handle. Nothing refers to a removed account's or
     * instance's series after this, so its rows and catalogue are forgotten
     * and the table does not keep them for the life of the process.
     */
    window_service_.release_scope(gone.instance_id, gone.account_id);
    if (gone.account_id.empty())
        series_.forget_instance(gone.instance_id);
    else
        series_.forget_scope(gone.instance_id, gone.account_id);
    static_cast<void>(save_windows());
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
    case event::event_kind::series_history:
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
