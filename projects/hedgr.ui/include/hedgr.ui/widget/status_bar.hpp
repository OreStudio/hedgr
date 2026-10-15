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
 * @brief How heavy what is on the bar is.
 *
 * The bar carries one line, so two messages wanting it at once is the case
 * this decides. It is a ladder and not two flags, because what an operator
 * must be able to do is tell a problem from a remark at a glance, and a
 * problem from a remark that is also an error by reading the line.
 *
 * The order is deliberate. A notice is something that happened and is
 * therefore already history: anything may replace it, including another
 * notice. A problem is something that is still true, and a notice about
 * something that just happened must not push it off the bar. An error is a
 * failure the operator has to act on: only another error may replace it,
 * because a store that refused a write outranks a remark about how the store
 * was opened.
 */
enum class status_severity : std::uint8_t {
    /** @brief Something that happened. Fades on its own. */
    notice = 0,
    /** @brief Something that is true now, until withdrawn or replaced. */
    problem = 1,
    /** @brief A failure the operator must act on. Stays until replaced. */
    error = 2,
};

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
     * Takes the bar from another notice or from a silent bar, and starts the
     * clock that takes it away again. It does not take the bar from a problem
     * or a failure: those outrank it, and a remark about something that just
     * happened must not be what hides the thing the operator has to act on.
     */
    void show_event(std::string_view message, std::int64_t now_ns);

    /**
     * @brief Declares something that is true until further notice.
     *
     * Raised every frame by whoever owns the condition, so it has to be
     * idempotent: a condition re-announced sixty times a second would be
     * noise, and would take the bar back from an event notice that landed in
     * between -- which is the one message the operator has not read yet.
     *
     * A raise of the condition that already holds is a restatement and changes
     * nothing, whether or not that condition is the thing the bar is showing:
     * an event notice may be on the bar over the top of a condition that still
     * holds, and that notice is the message the operator has not read yet.
     * A raise of a different condition is a new fact and takes the bar, unless
     * what is on it outranks the raise. See @c status_severity.
     *
     * @param message what is true now.
     * @param now_ns the frame's clock reading, kept for the fade and for callers
     * that want to know when the condition was raised.
     * @param level how heavy the message is, which decides what may replace it.
     */
    void show_condition(std::string_view message, std::int64_t now_ns,
        status_severity level = status_severity::problem);

    /**
     * @brief Reports a failure, which stays until the next one replaces it.
     *
     * Shorthand for a condition at @c status_severity::error, because that is
     * how every failure the runtime records arrives: raised again every frame
     * by whoever holds it, so it has to be idempotent for the same reason the
     * other conditions are.
     *
     * An error is deliberately not an event. An event notice describes
     * something that happened, ages out, and is worth missing if the operator
     * looked away; a failure is a state of the store that is still true when
     * they look back, and one that vanished after six seconds would let a
     * refused write read as a successful one. It is deliberately not cleared
     * by anything except another error or @c clear_error: the operator decides
     * when they have finished reading it, not the next thing that happens to
     * the store.
     */
    void show_error(std::string_view message, std::int64_t now_ns);

    /**
     * @brief Withdraws the condition, if the bar is showing one.
     *
     * An event on the bar is left alone: it describes something that happened
     * rather than the condition that has just ended, and cutting it short
     * would lose a fact the person has not finished reading.
     */
    void clear_condition();

    /**
     * @brief Withdraws an error, and only an error.
     *
     * What the operator's dismissal means. A notice or a problem on the bar is
     * left alone, because this is not a general clear: it is the answer to
     * "I have read the failure", and using it on anything else would take a
     * message away from somebody who did not ask.
     */
    void clear_error();

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
     * @brief How heavy what is on the bar is.
     *
     * What the bar itself carries, not what the last caller asked to raise: an
     * error that a notice outranked is not on the bar, and this reports the
     * bar. Silent is reported as @c status_severity::notice, which is the
     * lightest thing it could be and never outranks a raise.
     */
    [[nodiscard]] status_severity severity() const noexcept;

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
        /** @brief A failure, which only another failure may replace. */
        failure = 3,
    };

    /**
     * @brief How heavy a state is, which is what decides what may replace it.
     *
     * A silent bar weighs a notice, which is the lightest thing there is, so
     * anything may take it. An event weighs a notice too: it is a remark that
     * has not aged out yet, and a problem or a failure outranks it.
     */
    [[nodiscard]] static status_severity weight_of(kind value) noexcept;

    kind kind_ = kind::silent;

    /** @brief What is on the bar, for the event case and the condition case. */
    std::string message_;

    /**
     * @brief The condition that currently holds, empty when none does.
     *
     * Held apart from @c message_ because the two answer different questions:
     * this is what is true, and the message is what is being shown. An event
     * notice on the bar over a condition that still holds is exactly the case
     * where they differ.
     */
    std::string condition_;

    /**
     * @brief How heavy the condition that holds is.
     *
     * Kept beside @c condition_ for the same reason that one is kept beside
     * the message: the weight is a property of what is true, and it is what
     * decides whether the next raise may take the bar.
     */
    status_severity condition_level_ = status_severity::problem;

    std::int64_t shown_at_ns_ = 0;
    std::int64_t raised_at_ns_ = 0;
};

}

#endif
