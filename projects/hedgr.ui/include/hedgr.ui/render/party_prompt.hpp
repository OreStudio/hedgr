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
#ifndef HEDGR_UI_RENDER_PARTY_PROMPT_HPP
#define HEDGR_UI_RENDER_PARTY_PROMPT_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/series_browser_view.hpp"
#include <string>
#include <unordered_set>

namespace hedgr::ui::render {

/**
 * @brief What the sign-in's party prompt holds between frames.
 *
 * The prompt is offered once, right after a login that may act as more than one
 * party, and it is the first thing an operator answers. It asks for the session
 * rather than for a window: the party belongs to the session, so a desk with two
 * sessions gets two questions and neither window owns them.
 *
 * The choice is written by the runtime against the account once the instance
 * confirms it, so a session that has answered stops being asked without this
 * state having to be cleared by hand.
 */
struct party_prompt_state final {
    /**
     * @brief The session the prompt is showing for, or empty for none.
     *
     * Kept as its two keys rather than as a handle: a session that goes away
     * while the prompt is up must not be addressed, and the pair is what finds
     * it again next frame.
     */
    std::string instance_id;
    std::string account_id;

    /** @brief The parties the login may act as, and the one it acts as now. */
    party_list parties;

    /**
     * @brief Sessions whose prompt was put down without an answer.
     *
     * The title bar's close button is the only way to decline, and declining is
     * deferral rather than a choice: nothing is remembered, so the question
     * comes back on the next sign-in instead of a default being adopted
     * silently. The rows are the answer; there is no button that says "later"
     * and then means "the default".
     */
    std::unordered_set<std::string> dismissed;
};

/**
 * @brief The command that makes @p session act as the party at @p index.
 *
 * One row, one click, one command: the dialog has no confirm step because the
 * row the operator clicks *is* the answer. Exposed so a test can check that the
 * row names the party it claims to, which is the only thing that could go
 * silently wrong.
 */
[[nodiscard]] HEDGR_UI_EXPORT hedgr::model::command::command
party_choice_command(const party_prompt_state& state, std::size_t index);

/**
 * @brief Offers the party choice a sign-in needs, and applies the answer.
 *
 * Drawn last, like the lock prompt, so it is not covered by the windows the
 * sign-in opened. Nothing is drawn once every signed-in session has answered,
 * which is the ordinary frame of a running desk.
 */
HEDGR_UI_EXPORT void draw_party_prompt(const render_input& input,
    party_prompt_state& state);

}

#endif
