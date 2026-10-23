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
#ifndef HEDGR_IMGUI_APP_LOG_FILE_HPP
#define HEDGR_IMGUI_APP_LOG_FILE_HPP

#include <filesystem>
#include <string>

namespace hedgr::imgui::app {

/** @brief The file the log is written to when logging is on and none is named. */
inline constexpr const char* default_log_filename = "hedgr.imgui.log";

/** @brief Where a log file goes, as the two parts the logging options hold. */
struct log_target final {
    /** @brief The file's name. Never empty once resolved. */
    std::string filename;

    /** @brief The directory it is written in. */
    std::filesystem::path directory;
};

/**
 * @brief Fills in what a run that switched logging on left unsaid.
 *
 * ORE Studio's logging writes a file only when it is given a name, so
 * @c --log-enabled alone logs to the console and leaves no file behind, which
 * is how the last @c hedgr.log came to be dated days before the last run. A
 * missing name becomes @c default_log_filename. A missing directory becomes
 * @c log beside the store, where the layout file already lives, instead of the
 * launch directory, so the desktop entry and a terminal write the same file.
 * Whatever the caller did name is kept.
 *
 * @param named What the command line or environment gave.
 * @param database_path The store the run uses.
 */
[[nodiscard]] inline log_target resolve_log_target(log_target named,
    const std::filesystem::path& database_path) {
    if (named.filename.empty())
        named.filename = default_log_filename;
    if (named.directory.empty())
        named.directory = database_path.parent_path() / "log";
    return named;
}

}

#endif
