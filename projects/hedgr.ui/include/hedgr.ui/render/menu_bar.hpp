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
#ifndef HEDGR_UI_RENDER_MENU_BAR_HPP
#define HEDGR_UI_RENDER_MENU_BAR_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/** @brief What the bar's menus asked for this frame. */
struct menu_bar_result final {
    /** @brief The Instances window was opened and belongs on top. */
    bool focus_instances = false;

    /** @brief The open store should be locked. */
    bool lock_requested = false;

    /** @brief The password form should open, to unlock or to create. */
    bool unlock_requested = false;
};

/**
 * @brief Where the logo artwork lives at run time, or empty when it is nowhere.
 *
 * The same candidates the splash uses: beside the library under
 * share/hedgr.ui/branding, or the source tree for a run out of the build tree.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::filesystem::path logo_svg_path();

/** @brief The text of the file at @p path, or empty when it cannot be read. */
[[nodiscard]] HEDGR_UI_EXPORT std::string read_text_file(
    const std::filesystem::path& path);

/**
 * @brief Draws the top bar and, with it, insets the viewport's work area.
 *
 * The bar is a window of its own at the top of the main viewport, submitted
 * through ImGui's viewport side bar so the work area shrinks by its height
 * for every window and the dock space alike: this is the one place the top is
 * inset. ImGui applies the inset a frame after the bar is first drawn.
 *
 * It holds the logo, which opens the system menu. @p logo_svg is the logo's
 * document and may be empty, which leaves the button without its swirl. A new
 * top-level menu is one more @c widget::menu_bar_label call in here.
 */
HEDGR_UI_EXPORT void draw_menu_bar(const render_input& input,
    std::string_view logo_svg, widget::menu_bar_state& state,
    menu_bar_result& result);

}

#endif
