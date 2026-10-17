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
#include "hedgr.ui/image/svg_raster.hpp"
#include "lunasvg/lunasvg.h"
#include <algorithm>

namespace hedgr::ui::image {

std::optional<rgba_image> rasterize_svg(std::string_view svg, int width,
    int height) {
    if (width <= 0 || height <= 0)
        return std::nullopt;

    auto document = lunasvg::Document::loadFromData(svg.data(), svg.size());
    if (!document)
        return std::nullopt;

    auto bitmap = document->renderToBitmap(width, height, 0x00000000);
    if (bitmap.width() <= 0 || bitmap.height() <= 0)
        return std::nullopt;

    rgba_image result;
    result.width = bitmap.width();
    result.height = bitmap.height();
    result.pixels.resize(
        static_cast<std::size_t>(result.width) * result.height * 4);

    const auto* source = bitmap.data();
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const auto* pixel = source + y * bitmap.stride() + x * 4;
            const auto alpha = pixel[3];
            auto* target = result.pixels.data()
                + (static_cast<std::size_t>(y) * result.width + x) * 4;
            if (alpha == 0) {
                target[0] = target[1] = target[2] = target[3] = 0;
                continue;
            }
            target[0] = static_cast<std::uint8_t>(
                std::min(255, pixel[2] * 255 / alpha));
            target[1] = static_cast<std::uint8_t>(
                std::min(255, pixel[1] * 255 / alpha));
            target[2] = static_cast<std::uint8_t>(
                std::min(255, pixel[0] * 255 / alpha));
            target[3] = alpha;
        }
    }
    return result;
}

}
