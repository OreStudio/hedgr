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
#include "hedgr.runtime/worker/session_actor.hpp"
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
    : pool_(pool_threads), probe_pool_(probe_thread_count) {
    /*
     * The service is built from its member initialiser, which is what hands it
     * the tables and the lock getters, so a test's factory is installed here
     * rather than in the initialiser list. Nothing has opened an actor yet.
     */
    session_service_.set_factory(std::move(market_feeds));
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
    if (!started_ && session_service_.size() == 0 && probe_service_.idle())
        return;

    BOOST_LOG_SEV(lg(), ores::logging::info) << "Stopping the runtime";

    catalogue_rereads_.clear();
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
            return session_service_.wire_format_allows(row);
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
        complete_session(session_service_.connect(cmd));
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
        complete_session(session_service_.disconnect(cmd));
        break;
    case command::command_kind::select_party:
        complete_session(session_service_.select_party(cmd));
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
    case command::command_kind::configure_chart:
        /* One row rather than the wall, since every chart sends its first view. */
        if (window_service_.configure_chart(cmd) && window_service_.has_store()) {
            if (auto refused = window_service_.save_window(cmd.window_id))
                raise(std::move(refused->key), std::move(refused->message));
        }
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
         * Every session's actor is asked to stop and the manager stops being
         * started, but the pool stays up: pump() may still be called, and the
         * frame loop owns the decision about when there are no more frames.
         */
        session_service_.cancel();
        catalogue_rereads_.clear();

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

void instance_manager::report_store_failure(const std::string& instance_id,
    const std::string& account_id, std::string message) {
    event::event fact;
    fact.kind = event::event_kind::store_lock_failed;
    fact.instance_id = instance_id;
    fact.account_id = account_id;
    fact.message = std::move(message);
    events_.push(std::move(fact));
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

            retire_session(sessions_.instance_id(session),
                sessions_.account_id(session));
            market_feed_.detach(sessions_.instance_id(session),
                sessions_.account_id(session));

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
        retire_session(gone.instance_id, gone.account_id);
        market_feed_.detach(gone.instance_id, gone.account_id);
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

void instance_manager::complete_session(service::session_effect effect) {
    for (const auto& change : effect.conditions) {
        if (change.clear) {
            conditions_.clear(change.key);
            continue;
        }
        conditions_.raise(change.key, change.severity, change.message,
            steady_now_ns());
    }

    for (auto& refused : effect.refusals)
        raise(std::move(refused.key), std::move(refused.message));

    /*
     * The remembered party is asked for before anything the session reads, so
     * nothing is read for a party the operator did not choose. The ask is
     * asynchronous - the instance issues the token - so the party change's own
     * effect is what re-reads the catalogue.
     */
    if (effect.party)
        switch_party(effect.party->instance_id, effect.party->account_id,
            effect.party->party_id);

    for (const auto session : effect.unbind)
        window_service_.unbind(session);

    for (const auto& watched : effect.detach)
        market_feed_.detach(watched.instance_id, watched.account_id);

    for (const auto& watched : effect.attach)
        market_feed_.attach(watched.instance_id, watched.account_id,
            session_service_.feed(watched.instance_id, watched.account_id));

    for (const auto& watched : effect.refresh_watches)
        market_feed_.refresh_watches(watched.instance_id, watched.account_id);

    for (const auto& read : effect.reads)
        start_session_read(read);

    /*
     * The landing surface is decided by the fact and not by the click that
     * asked for it: a super administrator's session belongs to the system
     * tenant, and that is known only once IAM has answered. Opening it here
     * also means a refused sign-in leaves no window behind.
     */
    for (const auto& landing : effect.open)
        complete_open(window_service_.open_landing(landing.instance_id,
            landing.account_id, landing.kind));

    for (const auto& rebound : effect.rebind)
        window_service_.rebind(rebound.instance_id, rebound.account_id,
            rebound.session);

    if (effect.link_restored)
        market_feed_.link_restored(effect.link_restored->instance_id,
            effect.link_restored->account_id);

    if (effect.save_windows)
        static_cast<void>(save_windows());
}

void instance_manager::start_session_read(
    const service::session_effect::read_start& read) {
    switch (read.what) {
    case service::session_effect::read_start::kind::currency_image_map:
        read_service_.start_currency_image_map(read.instance_id,
            read.account_id, read.tenant_id);
        return;
    case service::session_effect::read_start::kind::market_catalogue:
        /*
         * The catalogue is party-scoped, so a party change reads it again
         * rather than filtering what the old party's read brought. A read
         * already in flight holds the pending mark and would swallow this one,
         * so the pair is remembered and the read is started when that answer
         * lands.
         */
        if (read_service_.market_catalogue_pending(read.instance_id,
                read.account_id)) {
            catalogue_rereads_.insert(
                running_key(read.instance_id, read.account_id));
            return;
        }
        if (auto refused = read_service_.start_market_catalogue_read(
                read.instance_id, read.account_id))
            raise(std::move(refused->key), std::move(refused->message));
        return;
    }
}

void instance_manager::switch_party(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    command::command request;
    request.kind = command::command_kind::select_party;
    request.instance_id = std::string{instance_id};
    request.account_id = std::string{account_id};
    request.party_id = std::string{party_id};
    complete_session(session_service_.select_party(request));
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
     * applied here rather than by the session service: routing it through a
     * session key would drop it, because an instance nobody has signed in to
     * has no session to find.
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
     * applied here for the same reason a probe result is: an unlock has no
     * session to be found by.
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
     * A read's answer is applied here rather than by the session service. The
     * caches it fills are keyed by name rather than by the session row, and the
     * answer is the only thing that clears the read's in-flight mark: dropping
     * it because the operator signed out meanwhile would leave the read marked
     * as running, and refuse it again after the next sign-in.
     */
    if (read_service_.apply(ev)) {
        /*
         * A restored window may name a series the catalogue no longer holds -
         * an instance re-provisioned since the wall was saved mints new ids -
         * and the answer that just landed is what can put it back on a series
         * that exists. The feed's next pass then watches and backfills it.
         */
        if (ev.kind == event::event_kind::series_catalogue)
            window_service_.resolve_series(ev.instance_id, ev.account_id);

        /*
         * A party change that wanted a catalogue read while another was in
         * flight is honoured here, now that the old answer has cleared the
         * pending mark. The new read then uses the new token.
         */
        if (ev.kind == event::event_kind::series_catalogue
            && catalogue_rereads_.erase(
                   running_key(ev.instance_id, ev.account_id)) > 0) {
            if (auto refused = read_service_.start_market_catalogue_read(
                    ev.instance_id, ev.account_id))
                raise(std::move(refused->key), std::move(refused->message));
        }
        return;
    }

    /*
     * Everything left is a fact about one session, and the session service owns
     * it: the row, the attempt, the actor, and what the fact means for them.
     * What the rest of the interface has to do about it - a landing window, a
     * rebind, a read, a line in the bar - comes back described, and is
     * performed by @c complete_session.
     */
    if (auto effect = session_service_.apply(ev))
        complete_session(std::move(*effect));
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

void instance_manager::retire_session(std::string_view instance_id,
    std::string_view account_id) {
    /*
     * Requesting a stop the actor has already observed is a no-op, and an actor
     * this pair never had is nothing to stop, so a teardown after a shutdown is
     * safe.
     */
    session_service_.retire(instance_id, account_id);
}

}
