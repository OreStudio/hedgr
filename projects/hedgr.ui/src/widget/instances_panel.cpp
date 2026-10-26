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
#include "hedgr.ui/widget/instances_panel.hpp"

#include <algorithm>
#include <cmath>

namespace hedgr::ui::widget {

namespace {

[[nodiscard]] char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool contains_folded(std::string_view text,
    std::string_view needle) noexcept {
    if (needle.size() > text.size())
        return false;
    for (std::size_t at = 0; at + needle.size() <= text.size(); ++at) {
        std::size_t i = 0;
        while (i < needle.size() && fold(text[at + i]) == fold(needle[i]))
            ++i;
        if (i == needle.size())
            return true;
    }
    return false;
}

}

bool instance_matches(std::string_view filter, std::string_view name,
    std::string_view id) noexcept {
    return filter.empty() || contains_folded(name, filter)
        || contains_folded(id, filter);
}

bool instance_less(std::string_view lhs_name, std::string_view lhs_id,
    std::string_view rhs_name, std::string_view rhs_id) noexcept {
    const auto less = [](std::string_view lhs, std::string_view rhs) {
        const std::size_t shared =
            lhs.size() < rhs.size() ? lhs.size() : rhs.size();
        for (std::size_t i = 0; i < shared; ++i) {
            const char a = fold(lhs[i]);
            const char b = fold(rhs[i]);
            if (a != b)
                return a < b;
        }
        return lhs.size() < rhs.size();
    };
    if (less(lhs_name, rhs_name))
        return true;
    if (less(rhs_name, lhs_name))
        return false;
    return less(lhs_id, rhs_id);
}

int tile_columns(float width, float tile_width, float gap) noexcept {
    const float pitch = tile_width + gap;
    if (pitch <= 0.0f)
        return 1;
    return std::max(1, static_cast<int>(std::floor((width + gap) / pitch)));
}

tile_offset tile_position(int index, int columns, float tile_width,
    float tile_height, float gap) noexcept {
    const int across = std::max(columns, 1);
    return {static_cast<float>(index % across) * (tile_width + gap),
        static_cast<float>(index / across) * (tile_height + gap)};
}

float tile_grid_height(int count, int columns, float tile_height,
    float gap) noexcept {
    if (count <= 0)
        return 0.0f;
    const int across = std::max(columns, 1);
    const int rows = (count + across - 1) / across;
    return static_cast<float>(rows) * tile_height
        + static_cast<float>(rows - 1) * gap;
}

}
