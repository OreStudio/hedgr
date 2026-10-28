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
#include "hedgr.runtime/worker/deployment_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "ores.logging/make_logger.hpp"
#include <chrono>
#include <string>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

/** @brief This read's logger. Counts are logged; secrets are not. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance =
        make_logger("hedgr.runtime.worker.deployment_worker");
    return instance;
}

/**
 * @brief The failure sentence a person reads, from the transport's answer.
 *
 * The code names the kind of failure and the message names what happened. Both
 * are needed: the sentence is the instruction and the code is what an engineer
 * is given when the sentence is not enough.
 */
[[nodiscard]] std::string describe(const transport::session_error& error) {
    if (error.message.empty())
        return std::string(transport::to_string(error.code));
    return error.message;
}

}

void run_deployment_read(transport::session_link& link, event::event& answer) {
    auto& snapshot = answer.deployment;

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Reading the deployment for '" << answer.instance_id << "' / '"
        << answer.account_id << "'";

    if (const auto read = link.deployment(); !read) {
        snapshot.failure = describe(read.error());
        snapshot.failing_read = "deployment";
    } else {
        snapshot.observed_at_ns =
            std::chrono::steady_clock::now().time_since_epoch().count();
        snapshot.tenant_total = read->tenant_total;
        snapshot.in_service = read->in_service;
        snapshot.on_evaluation = read->on_evaluation;
        snapshot.setting_up = read->setting_up;
        snapshot.needing_attention = read->needing_attention;
        snapshot.tenants.reserve(read->tenants.size());
        for (const auto& row : read->tenants) {
            snapshot.tenants.push_back(domain::tenant_row{.id = row.id,
                .name = row.name,
                .status = row.status,
                .type = row.type});
        }
    }

    if (!snapshot.failure.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The deployment read for '" << answer.instance_id
            << "' failed while " << snapshot.failing_read << ": "
            << snapshot.failure;
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Deployment read for '" << answer.instance_id << "' returned "
            << snapshot.tenant_total << " tenants";
    }
}

}
