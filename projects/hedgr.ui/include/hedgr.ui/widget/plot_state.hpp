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
#ifndef HEDGR_UI_WIDGET_PLOT_STATE_HPP
#define HEDGR_UI_WIDGET_PLOT_STATE_HPP

#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include <cstdint>
#include <string_view>

namespace hedgr::ui::widget {

/** @brief What a plot area says when it has no series to draw. */
enum class plot_state : std::uint8_t {
    /** @brief Nothing to say: the plot draws its data, or its own empty text. */
    none,
    /** @brief The session is not open, and nothing went wrong. */
    disconnected,
    /** @brief The session's last attempt failed. */
    failed,
    /** @brief A connection or a read is in flight. */
    reading,
    /** @brief The session is fine and the series' history read failed. */
    history_unavailable,
    /** @brief The series was read and holds no points. */
    no_data,
};

/**
 * @brief The state a plot area shows for its session.
 *
 * A session that is not open, or that failed, has no data coming, so it says
 * so whatever the series holds. A session that is connecting, or connected
 * with its series still being read, is waiting.
 *
 * @param status The window's session.
 * @param reading Whether the series' first read is still outstanding.
 */
[[nodiscard]] HEDGR_UI_EXPORT plot_state plot_state_for(
    hedgr::model::domain::session_status status, bool reading) noexcept;

/**
 * @brief Draws a rotating arc of @p radius round @p centre.
 *
 * A moving three quarters of a circle, its start angle taken from the ImGui
 * clock so no state is kept. The caller owns the frame that follows, which has
 * to come while this is on screen for the arc to turn.
 */
HEDGR_UI_EXPORT void draw_spinner(
    ImDrawList* draw, ImVec2 centre, float radius);

/**
 * @brief Draws @p state in the plot area @p min to @p max of the current window.
 *
 * A glyph of @p glyph_size centred in the area: the disconnected plug in a grey,
 * the failed plug in a brighter one, or the spinner. With @p captioned, a dim
 * caption sits below it and, for a failure, @p reason under that. Without,
 * only the glyph is drawn, for a card too small for words. @c none draws
 * nothing. The cursor is left moved, so a caller that cares restores it.
 */
HEDGR_UI_EXPORT void draw_plot_state(plot_state state, ImVec2 min, ImVec2 max,
    float glyph_size, bool captioned, std::string_view reason = {});

}

#endif
