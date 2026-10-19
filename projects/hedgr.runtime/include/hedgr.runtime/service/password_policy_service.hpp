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
#ifndef HEDGR_RUNTIME_SERVICE_PASSWORD_POLICY_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_PASSWORD_POLICY_SERVICE_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/password_policy_state.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <stop_token>

namespace hedgr::runtime::service {

/**
 * @brief The one read of the password policy the process makes.
 *
 * Shares the probe pool with the probe schedule and nothing else. Runs on the
 * frame thread; the read answers through the event channel.
 */
class password_policy_service final {
public:
    password_policy_service(const session::transport::session_options& seed_options,
        boost::asio::io_context& pool, model::command::event_queue& events);

    /** @brief What the deployment said a new password must satisfy. */
    [[nodiscard]] const model::state::password_policy_state&
    password_policy() const noexcept {
        return password_policy_;
    }

    /** @brief Whether the read has been asked for. */
    [[nodiscard]] bool requested() const noexcept { return policy_requested_; }

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

    /** @brief Cancels the read if it is still waiting on the transport. */
    void cancel() noexcept;

private:
    const session::transport::session_options& seed_options_;
    boost::asio::io_context& pool_;
    model::command::event_queue& events_;
    model::state::password_policy_state password_policy_;

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
