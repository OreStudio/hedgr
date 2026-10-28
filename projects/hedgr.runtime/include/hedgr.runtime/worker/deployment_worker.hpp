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
#ifndef HEDGR_RUNTIME_WORKER_DEPLOYMENT_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_DEPLOYMENT_WORKER_HPP

#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_link.hpp"

namespace hedgr::runtime::worker {

/**
 * @brief Fills the event with the deployment's snapshot, or with its failure.
 *
 * The connection is the session's, so there is no connect or resume here: the
 * read is one call for the tenant counts and the roster, and the whole result
 * travels in the event, which is what keeps the frame loop the only writer.
 *
 * A refusal fills the snapshot's own failure, because that is the field the
 * pane draws, and names the read that failed. It is not retried here: the retry
 * is the window's cadence, and a read that retried would hide the deployment's
 * state behind a second one.
 */
HEDGR_RUNTIME_EXPORT void run_deployment_read(
    hedgr::session::transport::session_link& link,
    model::event::event& answer);

}

#endif
