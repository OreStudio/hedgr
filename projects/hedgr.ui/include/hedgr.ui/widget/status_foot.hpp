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
#ifndef HEDGR_UI_WIDGET_STATUS_FOOT_HPP
#define HEDGR_UI_WIDGET_STATUS_FOOT_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include <cstdint>
#include <string>

namespace hedgr::ui::widget {

/**
 * @brief The state a window's foot reports, at its right end and as its dot.
 *
 * A value rather than a drawing call, because the foot knows how to lay a state
 * out and knows nothing about instances or accounts: each window resolves its
 * own selection to a word and a colour and hands the result over. That is what
 * lets the Instances window and the Accounts window share one foot without
 * either reaching into the other's tables.
 *
 * The name of the thing the state is about is deliberately not here. The pane
 * above already names the selection, and repeating it at the foot made the two
 * read as two different subjects.
 */
struct foot_state final {
    /** @brief The state's own word, or empty when there is no state. */
    std::string word;

    /** @brief The state's colour, for the word and for the dot. */
    ImU32 colour = 0;

    /**
     * @brief Who the window's session is and where it is, or empty.
     *
     * Drawn at the left in the disabled colour, and only while there is no
     * message to draw. A message is news and the identity is not, so the news
     * takes the line when it comes.
     */
    std::string identity;
};

/**
 * @brief How tall a window's foot is, whether or not anything is on it.
 *
 * Counted from the font rather than fixed, so a theme that installs a larger
 * face gets a foot that still fits its own text.
 */
[[nodiscard]] HEDGR_UI_EXPORT float status_foot_height(
    const status_bar& status);

/**
 * @brief Measures the foot of the window and draws the line into it.
 *
 * Both halves are needed and the split is the point. Reserving the space first
 * is what stops the table above from changing height as messages come and go:
 * a table that grew by two rows whenever something was announced would move
 * the row under the pointer at the worst possible moment. Drawing into a child
 * of its own is what pins the line to the window's foot instead of leaving it
 * wherever the browser happened to stop.
 *
 * A small coloured disc sits at the foot's right end, ahead of the state word
 * and carrying the same colour as it. A reader gets the state at a glance from
 * the dot and reads it in words beside it, which is what this interface asks of
 * a colour: it is learned, and the word is what a person reads the first time.
 *
 * @param status The bar whose message is drawn, advanced to this frame.
 * @param now_ns The frame's clock, for the fade.
 * @param state The selected thing's state, or an empty one for no state.
 */
HEDGR_UI_EXPORT void draw_status_foot(status_bar& status, std::int64_t now_ns,
    const foot_state& state);

}

#endif
