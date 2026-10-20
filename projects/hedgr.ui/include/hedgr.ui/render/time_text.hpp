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
#ifndef HEDGR_UI_RENDER_TIME_TEXT_HPP
#define HEDGR_UI_RENDER_TIME_TEXT_HPP

#include "hedgr.ui/export.hpp"
#include <cstdint>
#include <ctime>
#include <string>

namespace hedgr::ui::render {

/**
 * @brief The instant a plot is as of, in the one form every screen shares.
 *
 * ISO 8601 in UTC, because it is short, unambiguous and sorts lexically. A
 * timestamp without a zone is a different claim on every machine, and a reader
 * comparing two windows has no way to know which one is ahead.
 */
[[nodiscard]] inline std::string as_of_text(std::int64_t at_ns) {
    if (at_ns <= 0)
        return {};
    const auto seconds = static_cast<std::time_t>(at_ns / 1'000'000'000);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

/** @brief An age as OreStudio words one, for example "5 minutes ago". */
[[nodiscard]] HEDGR_UI_EXPORT std::string age_text(std::int64_t age_ns);

/**
 * @brief How old a tick is, in the fewest characters that still tell a late
 * feed from a dead one.
 *
 * Seconds under a minute, whole minutes to ten, then ">10m": a tick older than
 * that is stale beyond argument and its exact age is noise. An instant ahead of
 * this clock reads "ahead", because that is a clock fault and not a slow feed.
 */
[[nodiscard]] inline std::string tick_age_text(std::int64_t age_ns) {
    constexpr std::int64_t second = 1'000'000'000;
    if (age_ns < 0)
        return "ahead";
    const auto seconds = age_ns / second;
    if (seconds < 60)
        return std::to_string(seconds) + "s";
    const auto minutes = seconds / 60;
    if (minutes > 10 || (minutes == 10 && seconds > 600))
        return ">10m";
    return std::to_string(minutes) + "m";
}

/**
 * @brief The as-of instant with its age in front.
 *
 * The age goes before the instant so the instant, which the foot right-aligns,
 * stays where it is on every frame while the age changes width.
 */
[[nodiscard]] inline std::string as_of_age_text(std::int64_t at_ns,
    std::int64_t wall_now_ns) {
    const auto text = as_of_text(at_ns);
    if (text.empty() || wall_now_ns <= 0)
        return text;
    return tick_age_text(wall_now_ns - at_ns) + " \u00b7 " + text;
}

}

#endif
