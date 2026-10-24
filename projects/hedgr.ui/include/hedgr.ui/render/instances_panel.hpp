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
#ifndef HEDGR_UI_RENDER_INSTANCES_PANEL_HPP
#define HEDGR_UI_RENDER_INSTANCES_PANEL_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/instances_panel.hpp"

namespace hedgr::ui::render {

/**
 * @brief Draws the body of the INSTANCES menu's panel, inside its open popup.
 *
 * From the top, a row of tiles for the catalogue shortcuts, a filter box with
 * the shown and total counts, and a scrolling grid with one tile per instance:
 * its photo, or a glyph when it has none, its name, and a dot in the colour of
 * its probe state. A pick on a shortcut does what the removed shortcut bar did.
 * A pick on an instance opens or raises the Instances window, which has no
 * selection to drive from here. Either closes the popup unless Ctrl is held,
 * so several can be picked. A store with no instances, and a filter that
 * matches none, draw the no data mark with the words in its tooltip.
 *
 * Call it only while the popup is open. The filter is cleared and focused each
 * time the panel opens, which it knows from @c state.shown.
 */
HEDGR_UI_EXPORT void draw_instances_panel(const render_input& input,
    widget::instances_panel_state& state, menu_bar_result& result);

}

#endif
