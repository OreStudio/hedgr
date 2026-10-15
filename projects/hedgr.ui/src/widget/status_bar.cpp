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
#include "hedgr.ui/widget/status_bar.hpp"

namespace hedgr::ui::widget {

namespace {

/**
 * @brief How long a notice spends fading, in nanoseconds.
 *
 * The tail of the notice's life, not a period of its own: a notice is at full
 * strength until this much of it is left and then dims to nothing. Half a
 * second is long enough to read as a fade and short enough not to be a second
 * message.
 */
constexpr std::int64_t fade_ns = 500'000'000;

}

void status_bar::show_event(std::string_view message, std::int64_t now_ns) {
    if (message.empty()) {
        clear_condition();
        return;
    }
    kind_ = kind::event;
    message_ = std::string{message};
    shown_at_ns_ = now_ns;
}

void status_bar::show_condition(std::string_view message, std::int64_t) {
    if (message.empty()) {
        clear_condition();
        return;
    }

    /*
     * A restatement of the condition that already holds changes nothing. This
     * is not the same test as "the condition is what the bar is showing": an
     * event notice may be on the bar over the top of a condition that still
     * holds, and that is precisely the case this has to get right. Comparing
     * against the display instead of against the condition would let the next
     * frame's raise take the bar back from a notice the operator has not read,
     * and comparing only by text would miss a condition whose own wording
     * changed.
     */
    if (condition_ == message)
        return;

    condition_ = std::string{message};
    kind_ = kind::condition;
    message_ = std::string{message};
    shown_at_ns_ = 0;
}

void status_bar::clear_condition() {
    condition_.clear();

    /*
     * Only the condition leaves the bar. An event notice on the bar describes
     * something else that happened, and it is the message the operator has not
     * finished reading: a condition ending is not a reason to take it away.
     */
    if (kind_ != kind::condition)
        return;
    kind_ = kind::silent;
    message_.clear();
    shown_at_ns_ = 0;
}

void status_bar::advance(std::int64_t now_ns) noexcept {
    if (kind_ != kind::event)
        return;
    if (now_ns - shown_at_ns_ < status_event_duration_ns)
        return;

    kind_ = kind::silent;
    message_.clear();
    shown_at_ns_ = 0;
}

float status_bar::alpha(std::int64_t now_ns) const noexcept {
    if (kind_ != kind::event)
        return 1.0f;

    const auto elapsed = now_ns - shown_at_ns_;
    if (elapsed >= status_event_duration_ns)
        return 0.0f;

    /*
     * Counted from the end, because the fade is the last part of the life
     * rather than a period in the middle of it: everything before the tail is
     * a full-strength notice.
     */
    const auto remaining = status_event_duration_ns - elapsed;
    if (remaining >= fade_ns)
        return 1.0f;
    return static_cast<float>(remaining) / static_cast<float>(fade_ns);
}

}
