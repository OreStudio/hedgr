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
 * @brief Blurs the alpha of @p image with a gaussian of @p sigma pixels.
 *
 * Separable: one horizontal and one vertical pass of a kernel truncated at
 * three sigma, edges treated as transparent. The colour is left as it was, so
 * a white mask stays white and only its coverage softens. The total alpha is
 * kept as long as the shape is further than three sigma from the edge.
 */
[[nodiscard]] HEDGR_UI_EXPORT rgba_image blur_alpha(const rgba_image& image,
    float sigma);

/**
 * @brief An SVG's silhouette, blurred, on a larger transparent canvas.
 *
 * The document is rasterised at @p inner by @p inner pixels, centred on a
 * @p size by @p size canvas and blurred by @p sigma pixels, so the halo has the
 * room to fade out instead of being cut at the document's edge. This is the
 * soft light of a glow: drawn stretched over a rect @c size/inner times the
 * shape's, it hugs the shape. Nothing on an unusable document or sizes.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<rgba_image> rasterize_svg_glow(
    std::string_view svg, int size, int inner, float sigma);

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
