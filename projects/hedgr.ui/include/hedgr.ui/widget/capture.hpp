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
#ifndef HEDGR_UI_WIDGET_CAPTURE_HPP
#define HEDGR_UI_WIDGET_CAPTURE_HPP

#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief The kind of input a scripted capture step asks for.
 *
 * Five, because these are the gestures an operator uses on a browser and a
 * capture could not make: select a row, work its expander, and move the pointer
 * onto a control. A step that named a screen coordinate instead would test the
 * coordinate rather than the interface.
 */
enum class capture_step_kind : std::uint8_t {
    /** @brief Click the middle of a row, which is a click on its label. */
    click = 0,
    /** @brief Click the middle of a row's own expander arrow. */
    click_arrow = 1,
    /** @brief Click a row's arrow only when it is closed, so it ends open. */
    expand = 2,
    /** @brief Click a row's arrow only when it is open, so it ends closed. */
    collapse = 3,
    /** @brief Move the pointer onto a row without pressing anything. */
    focus = 4,
};

/**
 * @brief One gesture a capture script asks for, and what it names.
 *
 * The target is a row's name as the operator sees it -- "Development", "Brave
 * Hopper" -- rather than an id, because the question a frame answers is what a
 * person sees, and the tree's ids are derived from a store the script has never
 * read.
 */
struct capture_step final {
    /** @brief Which gesture. */
    capture_step_kind kind = capture_step_kind::click;

    /** @brief The row or control the gesture names. */
    std::string target;

    /** @brief The control's own label: a toolbar's tooltip, a button's text. */
    std::string label;

    /**
     * @brief The line this step came from, for a message naming it.
     *
     * A script is written by hand and a step that names no row has to say which
     * line to look at, because the row it named may simply be under a folder
     * that is shut.
     */
    std::size_t line = 0;
};

/**
 * @brief What the drawn interface showed, so a later frame can be driven to it.
 *
 * Recorded where the item is submitted, which is why a step resolves to the
 * rectangle ImGui itself computed. A click driven through this hits the same
 * rectangle a person's pointer would.
 */
struct capture_element final {
    /** @brief What a step names to reach it. */
    std::string target;

    /** @brief The control's own label, empty for a tree row. */
    std::string label;

    /** @brief The row's hit box, in screen coordinates. */
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;

    /**
     * @brief Where the row's label begins, which is past its expander arrow.
     *
     * A click that selects a row belongs on the text, not in the middle of the
     * row's full width, and on a tree row the two are far apart.
     */
    float label_min_x = 0.0f;

    /**
     * @brief How wide the row's expander arrow is.
     *
     * Zero for a row that has none, which is what a leaf is: a step that works
     * an expander is refused against a leaf rather than driven, because a click
     * where the arrow would be is a click on the leaf's own label.
     */
    float arrow_width = 0.0f;

    /**
     * @brief Whether the node was open on the frame the row was recorded.
     *
     * What decides whether an @c expand or a @c collapse step clicks at all, so
     * the script states the state it wants rather than the gesture it makes.
     */
    bool open = false;

    /** @brief The frame the row was last drawn on, which dates the rectangle. */
    std::uint32_t frame = 0;
};

/**
 * @brief Starts the run's script, which a capture gives through the environment.
 *
 * The steps come from @c HEDGR_CAPTURE_INPUT, one per line, as
 * @c "<gesture> <row> [| <label>]" where the gestures are @c click, @c
 * click_arrow, @c expand, @c collapse and @c focus. A row whose name holds a
 * space is written in quotes, as @c "Brave Hopper"; the label is separated by a
 * bar, and only by one.
 *
 * The label names which of two things of the same name is meant. An entry is on
 * screen twice, once in the tree and once in the list, and both are called the
 * same thing, so @c "click laboratory" means the tree row and
 * @c "click laboratory | list" means the list row.
 *
 * @c HEDGR_CAPTURE_INPUT_FILE names a file with the same contents, for a script
 * too long to pass through the environment.
 *
 * A run with no script is left entirely alone: nothing is recorded and no input
 * is queued, so the cost of this hook to an ordinary session is one flag test
 * per frame.
 */
HEDGR_UI_EXPORT void start_capture_run();

/**
 * @brief Parses step lines, which is the whole of the script's grammar.
 *
 * Split out from the environment so it can be tested without a context, a
 * window or a store, because a mistyped step names the line it is on rather
 * than silently driving nothing.
 *
 * @param text The lines, newline separated.
 * @return The steps, in the order given. A line whose first word is no known
 * gesture, and a line with no target, are skipped: a capture that refuses to
 * start is a check nobody runs.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::vector<capture_step>
parse_capture_steps(std::string_view text);

/**
 * @brief The frame a scripted capture's first gesture is driven on.
 *
 * Late enough that the store has opened and every window has been laid out,
 * which is what makes the rectangles a step resolves against the ones a person
 * would have seen.
 */
inline constexpr std::uint32_t capture_first_step_frame = 12;

/**
 * @brief The frames a gesture is spread over.
 *
 * Three: the pointer arrives, the button goes down, and it comes up. The
 * arrival is a frame of its own so the row is hovered before it is clicked, and
 * the frames between one gesture and the next are what let the interface draw
 * the result before the next gesture reads it.
 */
inline constexpr std::uint32_t capture_frames_per_step = 3;

/**
 * @brief Starts a capture frame: forgets what the last one drew.
 *
 * Called before anything is submitted, so what a step resolves to has been seen
 * on a frame whose rectangles are still on screen.
 *
 * @param frame The frame's own number.
 */
HEDGR_UI_EXPORT void begin_capture_frame(std::uint32_t frame);

/**
 * @brief Ends a capture frame: what it drew becomes what a step measures by.
 *
 * Called after the frame has been submitted. A gesture of a later frame has to
 * resolve against a frame that is already finished, so a frame is sealed here
 * rather than read as it is drawn.
 */
HEDGR_UI_EXPORT void end_capture_frame();

/**
 * @brief Puts the frame's pointer where the script left it, and keeps it there.
 *
 * Called immediately after the frame has begun, because the queue a gesture
 * wrote is drained inside that call and something else may have written the
 * pointer since: a backend that reports the desk's cursor, or ImGui itself when
 * it asks for the cursor to be moved. Either would take the gesture's pointer
 * away between the frames that make up a press, and a press whose pointer
 * arrived somewhere else is a click on whatever is there.
 *
 * A gesture of the current frame carries its own position, so this only has to
 * hold the pointer still on the frames between a move and the press it belongs
 * to.
 */
HEDGR_UI_EXPORT void hold_capture_pointer();

/**
 * @brief Records one submitted item, so a later frame can be driven to it.
 *
 * Called where the item is submitted rather than where the script is read,
 * which is why the rectangle is ImGui's own rather than one worked out here.
 *
 * @param target What a step names to reach this item.
 * @param element The rectangle ImGui computed for it.
 */
HEDGR_UI_EXPORT void observe_capture_element(std::string_view target,
    const capture_element& element);

/**
 * @brief Drives one frame of the script onto the running interface.
 *
 * Called once per frame, after the frame has begun and before anything is
 * drawn, because that is the frame the queue it writes is applied to: ImGui
 * reads its input queue when a frame begins, so a gesture added later would be
 * the next frame's and would land a frame late.
 *
 * A script is resolved once, against the rectangles an earlier frame drew, and
 * is a fixed timeline from then on: a step that named nothing on screen is left
 * out and reported on stderr with its line number, rather than the run quietly
 * capturing a frame the script did not ask for.
 *
 * @param frame The frame's own number, which is what places a step in time.
 */
HEDGR_UI_EXPORT void drive_capture_steps(std::uint32_t frame);

}

#endif
