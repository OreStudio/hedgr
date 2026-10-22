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
#ifndef HEDGR_IMGUI_APP_LAYOUT_FILE_HPP
#define HEDGR_IMGUI_APP_LAYOUT_FILE_HPP

#include <filesystem>
#include <string_view>

namespace hedgr::imgui::app {

/**
 * @brief Where ImGui keeps the window layout for a store.
 *
 * Beside the store rather than in the launch directory, so the desktop entry
 * and a terminal in the repository open the same arrangement and no stray file
 * is left wherever the binary was started.
 *
 * @param database_path The store the layout belongs to.
 * A window's saved position is a desktop coordinate where the platform can
 * place windows (X11) and a main-window coordinate where it cannot (Wayland).
 * The two do not mean the same thing, so each keeps its own file and switching
 * platform never moves a window by reading the other's positions. The file a
 * Wayland run uses keeps the old name so existing layouts survive.
 *
 * @param override_path The HEDGR_IMGUI_INI value, empty when unset. A capture
 *                      names its own file through it.
 * @param desktop_positions True when windows are placed by desktop position,
 *                          which is what ImGui's PlatformHasViewports reports.
 */
[[nodiscard]] inline std::filesystem::path layout_file_path(
    const std::filesystem::path& database_path,
    std::string_view override_path, bool desktop_positions = false) {
    if (!override_path.empty())
        return std::filesystem::path(override_path);
    return database_path.parent_path()
        / (desktop_positions ? "imgui-desktop.ini" : "imgui.ini");
}

}

#endif
