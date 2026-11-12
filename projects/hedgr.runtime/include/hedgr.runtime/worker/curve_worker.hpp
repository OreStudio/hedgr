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
#ifndef HEDGR_RUNTIME_WORKER_CURVE_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_CURVE_WORKER_HPP

#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/curve_table.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/service/curve_mapping.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace hedgr::runtime::worker {

/** @brief Each tenor's observed values, keyed by observation time. */
using pillar_history = std::map<std::string, std::map<std::int64_t, double>>;

/**
 * @brief Rebuilds the curve at the newest @p max_history instants.
 *
 * Each tenor's last value is carried forward to every instant, so every curve
 * holds every tenor that has data. An instant before some tenor's first
 * observation would yield a partial curve, so only instants from the latest
 * first observation onward are emitted; the newest instant always qualifies.
 *
 * The result is columnar, with the pillars in drawing order, so the model and
 * the plot take it as it is rather than sorting it once per frame.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT service::curve_reading
build_curve(const pillar_history& by_term, std::size_t max_history);

/**
 * @brief Fills @p answer with the curve @p request names, or why it failed.
 *
 * A published curve is one deployment call; any other curve is a tail per
 * tenor. Both answer in the same columnar shape, so the window that draws it
 * cannot tell which source it came from - and does not need to.
 */
HEDGR_RUNTIME_EXPORT void run_curve_read(
    hedgr::session::transport::session_link& link,
    const curve_request& request, model::event::event& answer);

}

#endif
