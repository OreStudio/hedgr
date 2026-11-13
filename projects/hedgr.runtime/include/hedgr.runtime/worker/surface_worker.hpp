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
#ifndef HEDGR_RUNTIME_WORKER_SURFACE_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_SURFACE_WORKER_HPP

#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.session/transport/session_link.hpp"

namespace hedgr::runtime::worker {

/**
 * @brief Fills @p answer with the volatility surface @p request names, or why
 * it could not be read.
 *
 * One call: a published surface is one calibrated object, so there is no
 * history to accumulate and no second source to fall back to. A deployment
 * that has published nothing at or before the instant refuses the read, and its
 * sentence is what the window shows - "no surface is published at or before" is
 * a fact about the deployment, not a failure of the client.
 */
HEDGR_RUNTIME_EXPORT void run_surface_read(
    hedgr::session::transport::session_link& link,
    const surface_request& request, model::event::event& answer);

}

#endif
