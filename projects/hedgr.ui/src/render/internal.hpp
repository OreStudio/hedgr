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
#ifndef HEDGR_UI_RENDER_INTERNAL_HPP
#define HEDGR_UI_RENDER_INTERNAL_HPP

/**
 * @brief The seam between the workspace and the Instance Manager.
 *
 * The toolbar's add-instance button is drawn by the workspace and the form it
 * opens is owned by the Instance Manager, so the click has to cross between
 * two translation units. It travels as this one flag, set during the toolbar
 * and read during the Instance Manager's body, both inside the same frame.
 *
 * It is deliberately not a field on render_input: a renderer signature that
 * carried "a button somewhere else was pressed" would have to be honoured by
 * every renderer, when exactly one of them cares.
 *
 * An inline variable with external linkage, not a file-scope static, because
 * a static in either file would be a separate object and the flag would never
 * arrive.
 */
namespace hedgr::ui::render::detail {

/** @brief Set by the toolbar, consumed by the Instance Manager. */
inline bool add_instance_requested = false;

}

#endif
