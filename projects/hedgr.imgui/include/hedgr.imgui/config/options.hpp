/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2025 Marco Craveiro <marco.craveiro@gmail.com>
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
#ifndef HEDGR_IMGUI_CONFIG_OPTIONS_HPP
#define HEDGR_IMGUI_CONFIG_OPTIONS_HPP

#include <filesystem>
#include <iosfwd>
#include <optional>
#include "ores.nats/config/nats_options.hpp"
#include "ores.logging/logging_options.hpp"

namespace hedgr::imgui::config {

/**
 * @brief All of the configuration options required by the client.
 */
struct options final {
    /**
     * @brief Configuration options related to logging, if any.
     */
    std::optional<ores::logging::logging_options> logging;

    /**
     * @brief Configuration options for connecting to the NATS broker.
     *
     * Absent when no subject prefix was supplied, in which case no instance is
     * seeded. This describes the instance to seed on a FIRST run only; once the
     * local store holds an instance, the store is authoritative and this is
     * ignored.
     */
    std::optional<ores::nats::config::nats_options> connection;

    /**
     * @brief Where the local store lives.
     *
     * Under the user's data directory, never under the install prefix: the
     * store holds this operator's instances and window wall, not the
     * application's.
     */
    std::filesystem::path database_path;

    /**
     * @brief Whether to start fullscreen.
     *
     * True by default. A trading desk wants the wall of windows, not a
     * windowed desktop behind it, and F11 toggles it either way.
     */
    bool fullscreen = true;
};

std::ostream& operator<<(std::ostream& s, const options& v);

}

#endif
