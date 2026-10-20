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
 * @brief The as-of instant with how old it is against @p wall_now_ns.
 *
 * A verdict word says that data is stale and not by how much. Seventy minutes
 * behind and ten seconds behind are different faults, one in the feed and one in
 * the link, and the age is what tells them apart at a glance. An instant ahead
 * of this clock says so, because that is a clock fault and not a slow feed.
 */
[[nodiscard]] inline std::string as_of_age_text(std::int64_t at_ns,
    std::int64_t wall_now_ns) {
    auto text = as_of_text(at_ns);
    if (text.empty() || wall_now_ns <= 0)
        return text;
    const auto age_ns = wall_now_ns - at_ns;
    text += age_ns < 0 ? " (ahead of this clock)"
                       : " (" + age_text(age_ns) + ")";
    return text;
}

}

#endif
