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
#ifndef HEDGR_UI_RENDER_SHORTCUT_BAR_HPP
#define HEDGR_UI_RENDER_SHORTCUT_BAR_HPP

#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/export.hpp"
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @brief What the bar remembers from one frame to the next.
 *
 * The tick exists to follow the pointer from one entry to another, and that is
 * a change, so it cannot be decided from a single frame. The one thing worth
 * keeping is the entry the pointer was on at the end of the last draw; every
 * other question about hover is answered by ImGui in the frame itself.
 */
struct shortcut_bar_state final {
    /** @brief The entry the pointer was on at the end of the last draw. */
    std::string hovered_id;
};

/**
 * @brief Whether @p hovered is an entry the tick should announce.
 *
 * A stationary pointer keeps naming the same entry and must stay silent, and a
 * pointer that has left the bar names nothing and must stay silent too. An
 * empty @p hovered therefore never fires, which is what stops the tick firing
 * every time the pointer leaves. The change from one entry to another is the
 * only change that does fire.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool hover_is_new_entry(
    std::string_view previous, std::string_view hovered) noexcept;

/**
 * @brief What the bar asked the frame to do, if any of it can only be done
 * while the windows are walked.
 *
 * Bringing a window to the front is not runtime state and not a command. It is
 * a thing about this frame's window stack, and only the walk that submits the
 * windows can do it, so the bar leaves the request here and the frame reads it
 * before the walk. A command would be a second writer of a fact the runtime
 * does not hold.
 */
struct shortcut_bar_result final {
    /** @brief Whether a shortcut asked for the Instances window to come up. */
    bool focus_instances = false;
};

/**
 * @brief Draws the shortcuts bar at the bottom of the viewport.
 *
 * The bar is a dock rather than a strip inside a window. Every window in this
 * interface floats and can be moved, resized or closed, and a bar belonging to
 * one of them would travel with that window and vanish with it, which is the
 * opposite of a shortcut. Anchored to the viewport it is always in the same
 * place and always reachable, and because it is its own window it takes no
 * space from the tree, the chart strip or the toolbar that share the Instances
 * window.
 *
 * The bar is drawn from the frame rather than from a renderer for the same
 * reason: there is one bar, not one per window.
 *
 * An empty bar draws nothing at all, so an operator who has put nothing on it
 * gets no strip of empty space across the bottom of their screen.
 *
 * @param input Everything the bar may read, including the entries themselves.
 * @param result Filled with whatever the frame has to do after the draw.
 * @param state The bar's own memory of the last hovered entry.
 */
HEDGR_UI_EXPORT void draw_shortcut_bar(const render_input& input,
    shortcut_bar_result& result, shortcut_bar_state& state);

}

#endif
