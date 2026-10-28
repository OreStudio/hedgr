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
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::runtime::service {

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
     * @brief @param events where the actors' answers go.
     * @param feeds makes and starts one actor; empty means a real
     * @c session_actor.
     */
    explicit session_service(model::command::event_queue& events,
        worker::tick_feed_factory feeds = {});

    session_service(const session_service&) = delete;
    session_service& operator=(const session_service&) = delete;

    /**
     * @brief Opens a session's actor, retiring the one it already had.
     *
     * @param options what the actor connects with. Its @c key names the
     * session, and its event queue is filled in here.
     * @return the actor, which the caller keeps for as long as it needs it.
     */
    [[nodiscard]] std::shared_ptr<worker::tick_feed>
    open(worker::session_actor_options options);

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

    /** @brief Stops every actor and forgets them all. */
    void cancel();

    /** @brief Forgets the actors whose threads have finished. */
    void reap();

    /** @brief Actors still answering reads. */
    [[nodiscard]] std::size_t size() const noexcept {
        return actors_.size();
    }

private:
    model::command::event_queue& events_;
    worker::tick_feed_factory feeds_;
    std::unordered_map<std::string, std::shared_ptr<worker::tick_feed>> actors_;
    std::vector<std::shared_ptr<worker::tick_feed>> retiring_;
};

}

#endif
