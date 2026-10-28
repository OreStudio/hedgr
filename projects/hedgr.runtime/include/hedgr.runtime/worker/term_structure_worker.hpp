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
#ifndef HEDGR_RUNTIME_WORKER_TERM_STRUCTURE_WORKER_HPP
#define HEDGR_RUNTIME_WORKER_TERM_STRUCTURE_WORKER_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace hedgr::runtime::worker {

/** @brief Each tenor's observed values, keyed by observation time. */
using pillar_history = std::map<std::string, std::map<std::int64_t, double>>;

/**
 * @brief Rebuilds the curves at the newest @p max_history instants.
 *
 * Each tenor's last value is carried forward to every instant. An instant
 * before some tenor's first observation would yield a partial curve, so only
 * instants from the latest first observation onward are emitted, and every
 * curve holds every tenor that has data. The newest instant always qualifies.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT std::vector<model::domain::term_curve>
build_term_curves(const pillar_history& by_term, std::size_t max_history);

/** @brief Fills the event's curves from each member's tail, or its failure. */
HEDGR_RUNTIME_EXPORT void run_term_structure_read(
    hedgr::session::transport::session_link& link,
    std::span<const std::string> ids, std::span<const std::string> tenors,
    model::event::event& answer);

}

#endif
