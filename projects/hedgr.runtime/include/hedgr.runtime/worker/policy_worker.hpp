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
#ifndef HEDGR_RUNTIME_WORKER_POLICY_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_POLICY_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.runtime/export.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <stop_token>

namespace hedgr::runtime::worker {

/**
 * @brief Everything the policy read needs, copied so the worker reads no state.
 *
 * The frame loop owns the state tables, so a worker receives copies of what it
 * needs and sends its result back as an event. That is the same contract every
 * other worker here follows.
 */
struct policy_job final {
    /** @brief Which deployment to ask. */
    session::transport::session_options options;

    /**
     * @brief The deployment, for the sentence a person reads when it fails.
     *
     * A failure on the create prompt is about a deployment as much as about a
     * connection: an operator who is told the address that did not answer knows
     * where to look, and one who is told only that something failed does not.
     */
    std::string subject_prefix;
};

/**
 * @brief Asks one deployment for its password policy and reports the answer.
 *
 * Runs on the pool like every other slow call, because the transport blocks.
 * Exactly one event is posted whichever way the attempt ends, including the
 * loss of the connection before the question was asked: the create prompt is
 * waiting on an answer either way, and a read that fails silently would leave
 * it saying "not yet" forever.
 *
 * @pre Run on an executor thread. Never call this from the frame loop.
 */
HEDGR_RUNTIME_EXPORT void
spawn_policy_read(boost::asio::io_context& context, policy_job job,
                  model::command::event_queue& events,
                  std::shared_ptr<std::stop_source> stop);

}

#endif
