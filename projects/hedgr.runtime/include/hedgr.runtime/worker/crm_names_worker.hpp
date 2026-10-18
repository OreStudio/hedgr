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
#ifndef HEDGR_RUNTIME_WORKER_CRM_NAMES_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_CRM_NAMES_WORKER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/session_task.hpp"
#include <boost/asio/io_context.hpp>
#include <string>

namespace hedgr::runtime::worker {

/** @brief One read of a party's enabled CRM names. */
struct crm_names_job final {
    /** @brief The signed-in session the read runs under. */
    session_read session;

    /** @brief The party whose CRM names are read. */
    std::string party_id;
};

/** @brief Reads the CRM names and posts them as a crm_names event. */
HEDGR_RUNTIME_EXPORT void spawn_crm_names_read(
    boost::asio::io_context& context, crm_names_job job,
    model::command::event_queue& events);

}

#endif
