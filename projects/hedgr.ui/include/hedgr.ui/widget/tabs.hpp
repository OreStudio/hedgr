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
#ifndef HEDGR_UI_WIDGET_TABS_HPP
#define HEDGR_UI_WIDGET_TABS_HPP

#include "hedgr.ui/export.hpp"

namespace hedgr::ui::widget {

/** @brief The thickness of the line under the chosen tab. */
inline constexpr float tab_underline_thickness = 2.0f;

/**
 * @brief Draws the accent line along the bottom of the tab just opened.
 *
 * Call it straight after a BeginTabItem that returned true, while the tab is
 * still the last item. ImGui draws its marker above a tab and only under a
 * flag; the reference marks the chosen tab from below, and this does that
 * without forking ImGui. The colour is ImGuiCol_TabSelectedOverline, which the
 * theme sets to the accent.
 */
HEDGR_UI_EXPORT void underline_selected_tab();

}

#endif
