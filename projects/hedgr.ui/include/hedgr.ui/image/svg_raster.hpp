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
#ifndef HEDGR_UI_IMAGE_SVG_RASTER_HPP
#define HEDGR_UI_IMAGE_SVG_RASTER_HPP

#include "hedgr.ui/export.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hedgr::ui::image {

/** @brief Straight-alpha RGBA pixels, the layout an OpenGL texture wants. */
struct rgba_image final {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

/**
 * @brief Rasterises an SVG document to straight-alpha RGBA.
 *
 * The deployment seeds its flags as SVG, which a browser draws natively and
 * stb cannot. lunasvg renders one to a pixel buffer, and this converts its
 * premultiplied ARGB into the RGBA an OpenGL texture upload expects. An
 * unusable document yields nothing rather than a blank flag.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<rgba_image>
rasterize_svg(std::string_view svg, int width, int height);

}

#endif
