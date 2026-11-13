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
#ifndef HEDGR_UI_RENDER_CURVE_LADDER_HPP
#define HEDGR_UI_RENDER_CURVE_LADDER_HPP

#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/curve_table.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @brief One rung of a curve, as a desk reads it.
 *
 * The ladder is the table a trader reads beside the curve: one row per pillar,
 * the value quoted at it now, and how old that value is. A row fills from two
 * sources and says which one it used:
 *
 * - the rung's own series, when the window watches it. That is what ticks, and
 *   it is the newer of the two whenever the feed is alive;
 * - the curve the plot draws, which is one read of the whole term structure at
 *   an instant. It is what a published curve has, because a published curve is
 *   one calibrated object and not a series per rung.
 *
 * There is a mid and no bid or ask: the deployment quotes one side, so a
 * ladder that filled two more columns would be inventing a book.
 */
struct ladder_rung final {
    /** @brief The pillar's name, borrowed from the curve axis. */
    std::string_view label;

    /** @brief The value quoted at the pillar, or nothing when none is. */
    std::optional<double> mid;

    /** @brief When that value was observed, or zero when there is none. */
    std::int64_t at_ns = 0;

    /**
     * @brief Whether the value came from the rung's own series.
     *
     * True means a tick: the row moves on its own. False means the curve read,
     * which is as fresh as the last whole-curve answer.
     */
    bool ticking = false;
};

/**
 * @brief Fills @p out with one rung per pillar of @p axis.
 *
 * @p out is sized by the caller, because a frame must not allocate: it writes
 * at most @c axis.pillars.size() entries and leaves the rest untouched.
 */
HEDGR_UI_EXPORT void curve_ladder(const render_input& input,
    hedgr::model::state::session_handle session,
    const hedgr::model::domain::term_structure_state& terms,
    const hedgr::model::state::curve_row& row,
    const hedgr::model::state::curve_axis& axis,
    std::span<ladder_rung> out);

}

#endif
