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
#ifndef HEDGR_UI_RENDER_PARTY_FOOT_HPP
#define HEDGR_UI_RENDER_PARTY_FOOT_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/status_foot.hpp"

namespace hedgr::ui::render {

/**
 * @brief The party a foot selection asks the session to act as, if any.
 *
 * Nothing when @p acting names no party or names the party the session already
 * holds: a foot redrawn every frame must not repeat the request.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<std::string> acted_party(
    const party_list& parties, int acting, std::string_view session_party);

/**
 * @brief Draws a window's foot with the session's "Acting as" control in it.
 *
 * The control is offered when the login may act as more than one party. A
 * choice pushes a `select_party` command, so every window that shows a
 * session's identity bar can change the party the same way.
 *
 * The window's own foot contents are untouched: the party control is appended
 * to them, so health, view state and identity text draw as before.
 *
 * @return whether the foot changed this frame.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool draw_foot_with_party(
    const render_input& input, model::state::session_handle session,
    hedgr::ui::widget::status_bar& status, hedgr::ui::widget::foot_state& foot,
    window_ui_state& state);

}

#endif
