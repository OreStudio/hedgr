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
#include <cmath>
#include <cstddef>
#include <cstdint>

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
        static_cast<std::size_t>(result.width)
        * static_cast<std::size_t>(result.height) * 4);

    const auto* source = bitmap.data();
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const auto* pixel = source + y * bitmap.stride() + x * 4;
            const auto alpha = pixel[3];
            auto* target = result.pixels.data()
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width)
                    + static_cast<std::size_t>(x)) * 4;
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

rgba_image blur_alpha(const rgba_image& image, float sigma) {
    if (sigma <= 0.0f || image.width <= 0 || image.height <= 0)
        return image;
    const int radius = static_cast<int>(std::ceil(3.0f * sigma));
    std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float w = std::exp(-0.5f * static_cast<float>(i * i) / (sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = w;
        sum += w;
    }
    for (float& w : kernel)
        w /= sum;

    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    std::vector<float> from(width * height);
    for (std::size_t i = 0; i < from.size(); ++i)
        from[i] = static_cast<float>(image.pixels[i * 4 + 3]);
    std::vector<float> into(from.size());
    const auto pass = [&](std::size_t along, std::size_t across,
                          std::size_t along_stride, std::size_t across_stride) {
        for (std::size_t line = 0; line < across; ++line) {
            for (std::size_t at = 0; at < along; ++at) {
                float total = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const auto probe = static_cast<std::ptrdiff_t>(at) + k;
                    if (probe < 0 || probe >= static_cast<std::ptrdiff_t>(along))
                        continue;
                    total += from[line * across_stride
                                  + static_cast<std::size_t>(probe) * along_stride]
                        * kernel[static_cast<std::size_t>(k + radius)];
                }
                into[line * across_stride + at * along_stride] = total;
            }
        }
    };
    pass(width, height, 1, width);
    from.swap(into);
    pass(height, width, width, 1);

    rgba_image result = image;
    for (std::size_t i = 0; i < into.size(); ++i)
        result.pixels[i * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp(std::lround(into[i]), 0L, 255L));
    return result;
}

std::optional<rgba_image> rasterize_svg_glow(std::string_view svg, int size,
    int inner, float sigma) {
    if (inner <= 0 || size < inner)
        return std::nullopt;
    const auto shape = rasterize_svg(svg, inner, inner);
    if (!shape)
        return std::nullopt;
    const auto side = static_cast<std::size_t>(size);
    const auto margin = static_cast<std::size_t>((size - shape->width) / 2);
    const auto shape_width = static_cast<std::size_t>(shape->width);
    rgba_image canvas;
    canvas.width = size;
    canvas.height = size;
    canvas.pixels.assign(side * side * 4, 0);
    for (std::size_t y = 0; y < static_cast<std::size_t>(shape->height); ++y) {
        for (std::size_t x = 0; x < shape_width; ++x) {
            const auto from = (y * shape_width + x) * 4;
            const auto to = ((y + margin) * side + x + margin) * 4;
            canvas.pixels[to + 0] = canvas.pixels[to + 1] = canvas.pixels[to + 2] = 255;
            canvas.pixels[to + 3] = shape->pixels[from + 3];
        }
    }
    auto blurred = blur_alpha(canvas, sigma);
    for (std::size_t i = 0; i < blurred.pixels.size(); i += 4)
        blurred.pixels[i + 0] = blurred.pixels[i + 1] = blurred.pixels[i + 2] = 255;
    return blurred;
}

}
