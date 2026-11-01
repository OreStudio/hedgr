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
#ifndef HEDGR_RUNTIME_SERVICE_SESSION_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_SESSION_SERVICE_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::runtime::service {

/**
 * @brief What the runtime must do after a session command or event.
 *
 * The session service owns what happens *to a session*: its row, its actor, its
 * connection and its attempt. What happens to the rest of the interface as a
 * result - a window losing its session, a landing window opening, the feed
 * starting to watch, a read starting - is described here and done by the
 * facade. That is the same shape @c catalogue_service returns, and the reason
 * the service holds no manager.
 */
struct session_effect final {
    /** @brief Refusals to raise, in the order they were decided. */
    std::vector<refusal> refusals;

    /** @brief Sessions whose windows must lose their binding. */
    std::vector<model::state::session_handle> unbind;

    /** @brief The attempt a pair is now on, so a stale fact can be told. */
    struct attempt final {
        std::string instance_id;
        std::string account_id;
        std::uint64_t number = 0;
    };
    std::optional<attempt> attempted;

    /** @brief Sessions the feed must stop watching. */
    struct watched final {
        std::string instance_id;
        std::string account_id;
    };
    std::vector<watched> detach;

    /** @brief Whether the window wall has to be written again. */
    bool save_windows = false;
};

/**
 * @brief What the session service may read and change outside its own actors.
 *
 * The tables and the two lock getters the plan names, plus the two ports it
 * cannot own: the store's path (which changes when the store opens) and the
 * account's sealed credential (which lives in the store's repository).
 */
struct session_dependencies final {
    const model::state::instance_table& instances;
    const model::state::account_table& accounts;
    model::state::session_table& sessions;
    session::transport::session_options seed;

    /** @brief Where the store is, or empty before it opens. */
    std::function<std::filesystem::path()> database_path;

    /** @brief The account's sealed credential, or nothing when it has none. */
    std::function<std::expected<std::optional<std::string>,
        store::sqlite::store_error>(std::string_view)>
        sealed_credential;

    /** @brief The store's lock state. */
    std::function<model::domain::store_lock()> store_lock;

    /** @brief The master password, when the store is unlocked. */
    std::function<model::domain::secret_string()> master_password;

    /** @brief Remembers a password for an account, when the store allows it. */
    std::function<void(std::string_view, std::string)> remember;
};

/**
 * @brief The actor of every live session, keyed by the session that owns it.
 *
 * One map from a session to the one connection it reads and plots on. The
 * market feed asks for the actor's tick feed and the read service asks it to
 * run a read, so both reach the same connection rather than each opening one
 * of their own.
 *
 * The actor owns its thread and its queue; this class only decides which
 * actor belongs to which session and keeps them until their threads have
 * closed. An actor that is retired keeps running until it has finished, and a
 * caller that already holds it keeps a working object, which is why the map
 * hands out @c shared_ptr rather than a bare reference.
 *
 * Runs on the frame thread and calls nothing that blocks: opening starts a
 * thread, and retiring asks it to stop. @c reap forgets the actors that have
 * stopped, and is safe to call when there are none.
 */
class HEDGR_RUNTIME_EXPORT session_service final {
public:
    /**
     * @brief A service with no supervision, for a test that only runs reads.
     *
     * @param events where the actors' answers go.
     * @param feeds makes and starts one actor; empty means a real
     * @c session_actor.
     */
    explicit session_service(model::command::event_queue& events,
        worker::tick_feed_factory feeds = {});

    /**
     * @brief A service that also supervises the sessions it holds.
     *
     * @param dependencies the tables and ports supervision reads.
     */
    session_service(model::command::event_queue& events,
        session_dependencies dependencies,
        worker::tick_feed_factory feeds = {});

    session_service(const session_service&) = delete;
    session_service& operator=(const session_service&) = delete;

    /**
     * @brief Whether a new connect to @p row may be made at all.
     *
     * The ORE client keeps one process-wide codec that every connect
     * overwrites, so a second format would corrupt the sessions already open
     * (C-5). Live means connecting or authenticated: a failed session has no
     * worker decoding anything, and a removed one has no row. A session of
     * @p row itself never conflicts, its format being @p row's own. Probes are
     * held back by the same answer, because their connect writes the same
     * global.
     */
    [[nodiscard]] bool wire_format_allows(
        const model::domain::instance& row) const;

    /**
     * @brief Signs a session in: the command's credential, or the stored one.
     *
     * Refuses when there is no instance or account, when another live session
     * speaks a different wire format, and when there is no password to try.
     * Creates the actor, which owns the connection from its first byte.
     */
    [[nodiscard]] session_effect connect(const model::command::command& cmd);

    /**
     * @brief Signs a session out and forgets its row.
     *
     * The actor is asked to sign out (SR-24) and the session's windows are
     * left open but unbound, because what the operator put on screen is not
     * theirs to take away.
     */
    [[nodiscard]] session_effect disconnect(const model::command::command& cmd);

    /** @brief Asks a live session to act as another party. */
    [[nodiscard]] session_effect select_party(
        const model::command::command& cmd);

    /**
     * @brief Opens a session's actor, retiring the one it already had.
     *
     * @param options what the actor connects with. Its @c key names the
     * session, and its event queue is filled in here.
     * @return the actor, which the caller keeps for as long as it needs it.
     */
    [[nodiscard]] std::shared_ptr<worker::tick_feed>
    open(worker::session_actor_options options);

    /**
     * @brief Replaces the factory that makes actors, before any is opened.
     *
     * A test installs a stand-in this way: the service is built from its
     * member initialiser, which is where its dependencies are handed over, so
     * the factory cannot be passed through the constructor as well.
     */
    void set_factory(worker::tick_feed_factory feeds);

    /** @brief The actor of one live session, or nothing. */
    [[nodiscard]] std::shared_ptr<worker::tick_feed> feed(
        std::string_view instance_id, std::string_view account_id) const;

    /**
     * @brief Asks a session's actor to run one read.
     *
     * False when the session has no actor, or its actor is stopping: nothing
     * was queued and nothing is in flight.
     */
    [[nodiscard]] bool submit_read(std::string_view instance_id,
        std::string_view account_id, worker::read_request request);

    /** @brief Stops a session's actor, keeping it until it has finished. */
    void retire(std::string_view instance_id, std::string_view account_id);

    /**
     * @brief Signs a session out and stops its actor.
     *
     * The one stop that says goodbye: the actor asks the instance to drop the
     * token before it closes, best effort, because the operator asked to sign
     * out and a token nobody will use again is still a credential (SR-24).
     */
    void sign_out(std::string_view instance_id, std::string_view account_id);

    /** @brief Stops every actor and forgets them all. */
    void cancel();

    /** @brief Forgets the actors whose threads have finished. */
    void reap();

    /** @brief Actors still answering reads. */
    [[nodiscard]] std::size_t size() const noexcept {
        return actors_.size();
    }

private:
    /** @brief The instance whose format a new connect may not contradict. */
    [[nodiscard]] const model::domain::instance* wire_format_holder(
        const model::domain::instance& row) const;

    /** @brief Creates the actor for one attempt, with its connection options. */
    void start(const model::command::command& cmd,
        model::domain::credential credential, std::string sealed_credential,
        session_effect& effect);

    model::command::event_queue& events_;
    std::optional<session_dependencies> deps_;
    worker::tick_feed_factory feeds_;
    std::uint64_t next_attempt_ = 0;
    std::unordered_map<std::string, std::shared_ptr<worker::tick_feed>> actors_;
    std::vector<std::shared_ptr<worker::tick_feed>> retiring_;
};

}

#endif
