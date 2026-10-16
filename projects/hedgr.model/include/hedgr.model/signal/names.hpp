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
#ifndef HEDGR_MODEL_SIGNAL_NAMES_HPP
#define HEDGR_MODEL_SIGNAL_NAMES_HPP

#include <string_view>

namespace hedgr::model::signal::names {

/*
 * Every name the application raises, in one file. A name is an interface the
 * moment a sound is registered against it or a status bar shows it, so a
 * rename that happened at the raise site alone would silently unhook the
 * listener. Keeping them together is the same discipline the icon table uses:
 * one place to read what exists.
 *
 * The shape is subject.condition.cause, dotted and lowercase, so a name reads
 * as what happened and so a registration can answer a whole prefix.
 */

/** @brief A deployment stopped answering as ready. */
inline constexpr std::string_view instance_readiness_degraded =
    "instance.readiness.degraded";

/** @brief A deployment that was not ready became ready. */
inline constexpr std::string_view instance_readiness_recovered =
    "instance.readiness.recovered";

/** @brief IAM answered that the deployment still needs setting up. */
inline constexpr std::string_view instance_readiness_bootstrapping =
    "instance.readiness.bootstrapping";

/** @brief A deployment stopped answering its broker at all. */
inline constexpr std::string_view instance_reachability_lost =
    "instance.reachability.lost";

/** @brief A deployment that was unreachable answered again. */
inline constexpr std::string_view instance_reachability_restored =
    "instance.reachability.restored";

/** @brief An entry on the shortcuts bar was clicked. */
inline constexpr std::string_view shortcut_activated = "shortcut.activated";

}

#endif
