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
#ifndef HEDGR_UI_ICON_FONTS_HPP
#define HEDGR_UI_ICON_FONTS_HPP

#include "hedgr.ui/export.hpp"

namespace hedgr::ui::icon {

/**
 * @brief Loads the base UI font and merges the icon font into it.
 *
 * Must be called inside an ImGui frame loop's setup, after the ImGui context
 * exists and before the rendering backend has built its texture atlas. The
 * atlas is built by the backend rather than here, because from ImGui 1.92 the
 * backend owns font texture creation and calling Build() is obsolete.
 *
 * A missing icon font is not fatal: the interface still draws, with the icon
 * positions occupied by the replacement glyphs of the base font. Losing every
 * window because a font file was not installed would be a worse trade.
 *
 * @param base_size_px Height of the base UI font, in pixels. The icon font is
 * merged at the same height so a glyph sits on the same baseline as the text
 * beside it.
 */
HEDGR_UI_EXPORT void load_ui_fonts(float base_size_px);

}

#endif
