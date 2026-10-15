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
#ifndef HEDGR_MODEL_DOMAIN_INSTANCE_HPP
#define HEDGR_MODEL_DOMAIN_INSTANCE_HPP

#include "hedgr.model/export.hpp"
#include <string>

namespace hedgr::model::domain {

/**
 * @brief A deployment of OreStudio that Hedgr can talk to.
 *
 * An instance is configuration rather than live state: it does not change while
 * Hedgr runs, it is persisted in the local store, and one Hedgr process holds
 * many. The NATS address and the subject prefix together identify the
 * deployment, because the same broker serves many deployments.
 *
 * The struct is a plain value with an explicit key and no owning pointers, so
 * rows join by key rather than by object graph.
 */
struct instance final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief Short human label, unique in the local store. */
    std::string name;

    /** @brief NATS server address, for example nats://localhost:20405. */
    std::string nats_url;

    /** @brief Subject prefix the deployment's services listen on. */
    std::string subject_prefix;

    /** @brief Wire format of the deployment's messages. */
    std::string wire_format = "msgpack";

    /** @brief Free-form labels, comma separated. Read only when shown. */
    std::string labels;

    /** @brief ISO-8601 creation timestamp. Read only when shown. */
    std::string created_at;
};

}

#endif
