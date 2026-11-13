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
#include "hedgr.runtime/worker/surface_worker.hpp"

#include "ores.logging/make_logger.hpp"
#include <chrono>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

/** @brief This worker's logger: what a surface read found, or why it could not. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.surface_worker");
    return instance;
}

}

void run_surface_read(hedgr::session::transport::session_link& link,
    const surface_request& request, model::event::event& answer) {
    /*
     * "The newest surface at or before now" is the desk's ordinary read, so
     * zero means the clock rather than the epoch: a request that stated no
     * instant is not a request for 1970.
     */
    const auto as_of_ns = request.as_of_ns != 0
        ? request.as_of_ns
        : std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();

    auto surface = link.published_fx_vol_surface(request.blueprint_id, as_of_ns);
    if (!surface) {
        /*
         * The deployment's own sentence is the one an operator can act on: it
         * says whether nothing is published at that instant or whether the read
         * was refused, and the two are different facts about the desk.
         */
        answer.message = surface.error().message.empty()
            ? surface.error().detail : surface.error().message;
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The surface of blueprint '" << request.blueprint_id
            << "' (" << request.label << ") could not be read: "
            << answer.message;
        return;
    }

    answer.blueprint_id = surface->blueprint_id.empty()
        ? request.blueprint_id : surface->blueprint_id;
    answer.curve_label = surface->name.empty() ? request.label : surface->name;
    answer.surface_as_of_ns = surface->as_of_ns;
    answer.surface_expiries = std::move(surface->expiries);
    answer.surface_strikes = std::move(surface->strikes);
    answer.surface_values = std::move(surface->values);

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Surface '" << answer.curve_label << "' read: "
        << answer.surface_expiries.size() << " expiries by "
        << answer.surface_strikes.size() << " strikes, "
        << answer.surface_values.size() << " values";
}

}
