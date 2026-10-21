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
#include <filesystem>

struct ImFont;

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

/**
 * @brief The monospaced face for numbers, or the base face when there is none.
 *
 * Prices and time ticks are read as digits in columns, and a proportional face
 * lets a changing digit move the ones beside it. Every number the interface
 * shows for a market is set in this face; prose is not. When no monospaced font
 * is installed the base face is returned, so a caller never has to check.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImFont* numeric_font();

/**
 * @brief The bold weight of the base face, or the base face when there is none.
 *
 * Found beside the base face on disk, so a value in bold is the same family as
 * the caption above it. Hedgr ships no font files and this adds none: a desk
 * without the bold file draws values in the regular weight.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImFont* bold_font();

/**
 * @brief The directory holding the per-environment instance illustrations, or
 * an empty path when there is none.
 *
 * Found by the same rule the icon sheet is found by, because both are assets
 * that ship beside this component and both have to be reachable from a run
 * straight out of the build tree and from an installed package. A caller that
 * decodes the files walks this directory rather than naming one, because the
 * set of labels is the store's to decide and this component only knows where
 * an asset of a given name would be.
 *
 * The file for a key is the key with a png extension. An empty path means the
 * illustrations are not installed, which is not an error: an interface with no
 * pictures draws its own fallback for every instance.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::filesystem::path instance_icon_directory();

}

#endif
