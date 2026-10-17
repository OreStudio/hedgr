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
#ifndef HEDGR_RUNTIME_WORKER_SESSION_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_SESSION_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.runtime/export.hpp"
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>

namespace hedgr::runtime::worker {

/**
 * @brief Everything a worker needs, copied so the worker never reads state.
 *
 * The frame loop owns the state tables outright. A worker therefore receives
 * copies of the handful of facts it needs and sends results back as events. It
 * touches no table, holds no handle to one, and takes no lock on the state.
 */
struct session_job final {
    /** @brief Where to connect and how. */
    session::transport::session_options options;

    /** @brief The pair this session belongs to, used to tag its events. */
    model::domain::session_key key;

    /** @brief The secret for this attempt. Cleared when authenticate returns. */
    model::domain::credential credential;

    /**
     * @brief The stored credential, still sealed, when one has to be opened.
     *
     * Either this or @c credential is filled, never both. A credential typed at
     * the prompt travels in @c credential; a credential remembered by the store
     * travels here still sealed, and the worker opens it on its own thread,
     * because the key derivation behind it would stall the frame loop. Opening
     * it is the worker's first act, so the plaintext exists for exactly as long
     * as the sign-in does and never reaches a caller of the manager.
     */
    std::string sealed_credential;

    /**
     * @brief The master password this attempt may open @c sealed_credential
     * with.
     *
     * Required when @c sealed_credential is set and ignored otherwise, so the
     * two cannot disagree about which secret is being used.
     */
    model::domain::secret_string master_password;

    /** @brief How long to wait between refresh round trips once authenticated. */
    std::chrono::milliseconds refresh_interval{2000};

    /**
     * @brief Which attempt at this pair this worker is.
     *
     * Every fact it posts carries this number, so the frame loop can tell a
     * stopped worker's last words from the worker that replaced it.
     */
    std::uint64_t attempt = 0;
};

/**
 * @brief Drives one session's whole lifecycle as a coroutine.
 *
 * Connects, authenticates, then refreshes until asked to stop. Every outcome is
 * posted to @p events as a fact; nothing is written anywhere else. A connect
 * failure is reported and the coroutine ends rather than retrying, because an
 * instance that answers nothing needs a decision from the operator and not
 * a retry storm. A retry belongs behind an explicit operator action.
 *
 * @pre Run on an executor thread. Never call this from the frame loop.
 */
HEDGR_RUNTIME_EXPORT void
spawn_session(boost::asio::io_context& context,
              session_job job,
              model::command::event_queue& events,
              std::shared_ptr<std::stop_source> stop);

}

#endif
