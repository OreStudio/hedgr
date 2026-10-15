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

void status_bar::show_condition(std::string_view message) {
    if (message.empty()) {
        clear_condition();
        return;
    }

    /*
     * Only the first raise replaces what is on the bar. A condition is raised
     * every frame by whatever owns it, and re-announcing it would wipe an
     * event notice that landed in between -- so a person would lose the one
     * message they had not read yet in favour of the one they had.
     */
    if (kind_ == kind::condition)
        return;

    kind_ = kind::condition;
    message_ = std::string{message};
    shown_at_ns_ = 0;
}

void status_bar::clear_condition() {
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
