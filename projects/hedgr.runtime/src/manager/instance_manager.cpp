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
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <chrono>
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
namespace transport = hedgr::session::transport;

/**
 * @brief The key a running worker is registered under.
 *
 * The id separator is the one the session table uses internally to join a pair
 * as well, so a key here can never collide with either half of another pair:
 * "a" + "bc" and "ab" + "c" have to be two workers, not one.
 */
[[nodiscard]] std::string running_key(std::string_view instance_id,
    std::string_view identity_id) {
    std::string result;
    result.reserve(instance_id.size() + identity_id.size() + 1);
    result.append(instance_id);
    result.push_back('\x1f');
    result.append(identity_id);
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
    : pool_(pool_threads) {
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
     * The pool is stopped here and not in do_shutdown: pump() may still be
     * called after a shutdown command, because the frame loop decides when the
     * last frame is, and a pump over a stopped context is harmless while a
     * joined one is not.
     */
    pool_.stop();
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
    identity_repo_ = std::make_unique<store::repository::identity_repository>(
        *database_);
    window_repo_ = std::make_unique<store::repository::window_repository>(
        *database_);

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
        instances_.upsert(row);

    auto identities = identity_repo_->list();
    if (!identities) {
        last_error_ = store_failure("cannot read the stored identities",
            identities.error());
        return std::unexpected(last_error_);
    }
    identities_.clear();
    for (const auto& row : identities.value())
        identities_.upsert(row);

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
                spec.session->identity_id)) {
            windows_.bind_session(handle, *session);
        }
    }

    seed_first_run();
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
    row.tier = domain::tier::development;
    row.nats_url = seed_options_.nats_url;
    row.subject_prefix = seed_options_.subject_prefix;
    row.wire_format = seed_options_.wire_format;
    row.created_at = iso8601_now();

    instances_.upsert(row);

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
    for (const auto& request : commands_.drain())
        handle(request);
    for (const auto& fact : events_.drain())
        apply(fact);
}

void instance_manager::handle(const command::command& cmd) {
    switch (cmd.kind) {
    case command::command_kind::save_instance:
        do_save_instance(cmd);
        break;
    case command::command_kind::remove_instance:
        do_remove_instance(cmd);
        break;
    case command::command_kind::save_identity:
        do_save_identity(cmd);
        break;
    case command::command_kind::remove_identity:
        do_remove_identity(cmd);
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
        started_ = false;
        break;
    }
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
    const auto* existing = instances_.find(cmd.instance_row.id);
    const auto previous = existing ? std::optional<domain::instance>(*existing)
                                   : std::nullopt;

    instances_.upsert(cmd.instance_row);

    if (auto saved = instance_repo_->save(cmd.instance_row); !saved) {
        if (previous)
            instances_.upsert(*previous);
        else
            instances_.remove(cmd.instance_row.id);
        last_error_ = store_failure(
            std::format("cannot save instance {}", cmd.instance_row.id),
            saved.error());
    }
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

    instances_.remove(id);
    identities_.remove_for_instance(id);

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
            sessions_.identity_id(session)));

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
    if (identity_repo_) {
        if (auto removed = identity_repo_->remove_for_instance(id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove the identities of instance {}", id),
                removed.error());
        }
    }
    if (auto saved = save_windows(); !saved)
        last_error_ = saved.error();
}

void instance_manager::do_save_identity(const command::command& cmd) {
    if (!identity_repo_) {
        last_error_ = "cannot save an identity: the store is not open";
        return;
    }

    const auto* existing = identities_.find(cmd.identity_row.id);
    const auto previous = existing ? std::optional<domain::identity>(*existing)
                                   : std::nullopt;

    identities_.upsert(cmd.identity_row);

    if (auto saved = identity_repo_->save(cmd.identity_row); !saved) {
        if (previous)
            identities_.upsert(*previous);
        else
            identities_.remove(cmd.identity_row.id);
        last_error_ = store_failure(
            std::format("cannot save identity {}", cmd.identity_row.id),
            saved.error());
    }
}

void instance_manager::do_remove_identity(const command::command& cmd) {
    const std::string id = cmd.identity_id.empty() ? cmd.identity_row.id
                                                   : cmd.identity_id;
    if (id.empty()) {
        last_error_ = "cannot remove an identity: no identity was named";
        return;
    }

    /*
     * One identity is at most one session, because a session is keyed by the
     * pair, so the row to stop is looked up by the pair rather than walked for.
     */
    const auto* row = identities_.find(id);
    const std::string instance_id = row ? row->instance_id : cmd.instance_id;

    identities_.remove(id);

    if (const auto session = sessions_.find(instance_id, id)) {
        stop_session(running_key(instance_id, id));
        for (const auto window : live_windows(windows_)) {
            if (windows_.session_of(window) == *session)
                windows_.clear_session(window);
        }
        sessions_.remove(*session);
    }

    if (identity_repo_) {
        if (auto removed = identity_repo_->remove(id); !removed) {
            last_error_ = store_failure(
                std::format("cannot remove identity {}", id), removed.error());
        }
    }
    if (auto saved = save_windows(); !saved)
        last_error_ = saved.error();
}

void instance_manager::do_connect_session(const command::command& cmd) {
    const auto* instance_row = instances_.find(cmd.instance_id);
    if (instance_row == nullptr) {
        last_error_ = std::format("cannot connect: no instance {}",
            cmd.instance_id);
        return;
    }

    const auto* identity_row = identities_.find(cmd.identity_id);
    if (identity_row == nullptr) {
        last_error_ = std::format("cannot connect: no identity {}",
            cmd.identity_id);
        return;
    }

    /*
     * Reconnecting a pair resumes the same session rather than making a second
     * one, which is the whole reason the table is keyed by the pair.
     */
    auto handle = sessions_.find(cmd.instance_id, cmd.identity_id);
    if (!handle)
        handle = sessions_.insert(cmd.instance_id, cmd.identity_id);
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
    job.key = domain::session_key{cmd.instance_id, cmd.identity_id};
    job.credential = domain::credential{cmd.principal, cmd.password};

    const auto key = running_key(cmd.instance_id, cmd.identity_id);
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

void instance_manager::do_disconnect_session(const command::command& cmd) {
    stop_session(running_key(cmd.instance_id, cmd.identity_id));

    const auto handle = sessions_.find(cmd.instance_id, cmd.identity_id);
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
    if (const auto session = sessions_.find(cmd.instance_id, cmd.identity_id))
        last_error = sessions_.last_error(*session);
    spec.state = default_state_for(cmd.new_window_kind, last_error);

    const auto handle = windows_.insert(spec);

    /*
     * A window opened from a session is bound to it, so the frame loop can
     * reach its state without a key lookup per window. A window opened with no
     * session named stays unbound and renders the session-loss state.
     */
    if (const auto session = sessions_.find(cmd.instance_id, cmd.identity_id))
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

void instance_manager::apply(const event::event& ev) {
    const auto handle = sessions_.find(ev.instance_id, ev.identity_id);

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
        sessions_.set_identity_result(*handle, ev.tenant_name, ev.party_name,
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
