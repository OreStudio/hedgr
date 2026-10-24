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
#ifndef HEDGR_UI_WIDGET_INSTANCES_PANEL_HPP
#define HEDGR_UI_WIDGET_INSTANCES_PANEL_HPP

#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/** @brief The size of the filter box's text buffer, terminator included. */
inline constexpr std::size_t instance_filter_capacity = 64;

/**
 * @brief The text typed into the Instances panel's filter box.
 *
 * A fixed buffer owned by the menu state, so ImGui edits it in place and the
 * frame allocates nothing. The panel clears it each time it opens.
 */
struct instance_filter final {
    char text[instance_filter_capacity] = {};

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(text);
    }
};

/**
 * @brief Whether an instance with this @p name and @p id passes @p filter.
 *
 * The filter is a case-insensitive substring over either field. ASCII is
 * folded and other bytes compare as they are. An empty filter passes
 * everything. Nothing is allocated.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool instance_matches(std::string_view filter,
    std::string_view name, std::string_view id) noexcept;

/**
 * @brief How many tiles of @p tile_width fit a row @p width wide, at least one.
 *
 * Tiles are @p gap apart, with none after the last, so a row holds
 * floor((width + gap) / (tile_width + gap)) of them.
 */
[[nodiscard]] HEDGR_UI_EXPORT int tile_columns(float width, float tile_width,
    float gap) noexcept;

/** @brief Where a tile's top left corner is, from the grid's own top left. */
struct tile_offset final {
    float x = 0.0f;
    float y = 0.0f;
};

/** @brief The offset of the tile at @p index, filling each row left to right. */
[[nodiscard]] HEDGR_UI_EXPORT tile_offset tile_position(int index, int columns,
    float tile_width, float tile_height, float gap) noexcept;

/** @brief The height of @p count tiles laid out in @p columns, zero for none. */
[[nodiscard]] HEDGR_UI_EXPORT float tile_grid_height(int count, int columns,
    float tile_height, float gap) noexcept;

/**
 * @brief What the Instances panel keeps between frames.
 *
 * The filter text is a fixed buffer ImGui edits in place. The filtered list is
 * rebuilt only when the text or the instance table's version differs from the
 * one it was built for, so a steady frame walks nothing and allocates nothing.
 */
struct instances_panel_state final {
    /** @brief The text in the filter box. */
    instance_filter filter;

    /** @brief The text @c matches was built for. */
    instance_filter applied;

    /** @brief The table version @c matches was built for. */
    std::uint64_t version = 0;

    /** @brief Whether @c matches was built at all since the panel opened. */
    bool built = false;

    /** @brief Whether the panel was drawn last frame, to see it open. */
    bool shown = false;

    /** @brief The slab indices of the instances that pass the filter, in order. */
    std::vector<std::uint32_t> matches;

    /** @brief How many instances there are, for the count beside the filter. */
    std::size_t total = 0;
};

}

#endif
