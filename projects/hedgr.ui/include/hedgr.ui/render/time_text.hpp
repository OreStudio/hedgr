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
#include <string_view>

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

/**
 * @brief Reads an ISO-8601 UTC instant, or returns zero when it cannot.
 *
 * The CRM service reports its batch time as text rather than as an instant on
 * the wire. This is the one place that text becomes nanoseconds, so every
 * screen can judge and draw the same value instead of parsing it twice. The
 * grammar is OreStudio's wire shape: either separator, an optional fraction,
 * and a UTC designator. A non-zero offset or a missing designator reads as
 * zero rather than as the wrong instant.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t parse_iso8601_utc(
    std::string_view text);

/** @brief An age as OreStudio words one, for example "5 minutes ago". */
[[nodiscard]] HEDGR_UI_EXPORT std::string age_text(std::int64_t age_ns);

}

#endif
