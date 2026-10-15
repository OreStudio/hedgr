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
#ifndef HEDGR_UI_WIDGET_STATUS_BAR_HPP
#define HEDGR_UI_WIDGET_STATUS_BAR_HPP

#include "hedgr.ui/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::ui::widget {

/** @brief How long an event notice stays on the bar, in nanoseconds. */
inline constexpr std::int64_t status_event_duration_ns = 6'000'000'000;

/**
 * @brief The short messages a window says about itself, in one line.
 *
 * Two kinds of message, and the difference between them is the whole design.
 *
 * An @e event describes something that just happened -- the store was opened
 * from the environment, a row was removed. It is true for a moment and then it
 * is history, so it is shown for a fixed time and then goes. A notice that
 * stayed would stop being read after the first glance and would push the
 * window's own content around for the rest of the session.
 *
 * A @e condition describes something that is true now -- a deployment is
 * unreachable. It holds until whoever raised it says otherwise, because it is
 * not history and it must not fade while it is still the case.
 *
 * The two share one line and one piece of scratch, so only one is on the bar
 * at a time, and the newest wins. That is a deliberate choice and not a
 * shortcut: this bar is one line under a table of instances, and a stack of
 * three messages under a table would be a second list for the eye to read
 * while the first one scrolls. Where two facts compete for one line, the one
 * that just happened is the one the person has not seen yet, so the newest
 * winning loses the least.
 *
 * The clock is the frame's monotonic one rather than the wall clock, so a
 * machine whose time is stepped does not hold a notice forever or drop one
 * instantly.
 */
class HEDGR_UI_EXPORT status_bar final {
public:
    /**
     * @brief Reports something that happened and is now history.
     *
     * Replaces whatever was on the bar, condition or event, and starts the
     * clock that takes it away again.
     */
    void show_event(std::string_view message, std::int64_t now_ns);

    /**
     * @brief Declares something that is true until further notice.
     *
     * Raised every frame by whoever owns the condition. The first raise
     * replaces the bar's contents; later ones only move the clock, so a
     * condition that is still true is not re-announced frame after frame.
     */
    void show_condition(std::string_view message);

    /**
     * @brief Withdraws the condition, if the bar is showing one.
     *
     * An event on the bar is left alone: it describes something that happened
     * rather than the condition that has just ended, and cutting it short
     * would lose a fact the person has not finished reading.
     */
    void clear_condition();

    /**
     * @brief Ages the notice, dropping an event once its time is up.
     *
     * Called once per frame by the thing that draws the bar, because the bar
     * has no clock of its own and is not a thread. A condition is never aged:
     * only whoever raised it can end it.
     */
    void advance(std::int64_t now_ns) noexcept;

    /** @brief The line to draw, or empty when there is nothing to say. */
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    /** @brief Whether anything is on the bar. */
    [[nodiscard]] bool empty() const noexcept { return message_.empty(); }

    /**
     * @brief How far through its life an event notice is, from 0 to 1.
     *
     * One for a condition, which has no life to run out of, and one for a
     * notice in its first moments. It is what lets the bar dim a notice as it
     * goes rather than have it disappear between one frame and the next, which
     * reads as a glitch rather than as a message ending.
     */
    [[nodiscard]] float alpha(std::int64_t now_ns) const noexcept;

private:
    /** @brief What the bar is currently carrying. */
    enum class kind : std::uint8_t {
        /** @brief Nothing: the line is not drawn at all. */
        silent = 0,
        /** @brief Something that happened, with a deadline. */
        event = 1,
        /** @brief Something that is true, until withdrawn. */
        condition = 2,
    };

    kind kind_ = kind::silent;
    std::string message_;
    std::int64_t shown_at_ns_ = 0;
};

}

#endif
