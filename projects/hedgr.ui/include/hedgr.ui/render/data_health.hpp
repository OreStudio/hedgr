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
#ifndef HEDGR_UI_RENDER_DATA_HEALTH_HPP
#define HEDGR_UI_RENDER_DATA_HEALTH_HPP

#include <cstdint>
#include <format>
#include <string>

namespace hedgr::ui::render {

/** @brief How fresh the data behind a plot is. */
enum class data_health : std::uint8_t {
    /** @brief A tick arrived within the live window. */
    live = 0,
    /** @brief The newest tick is old enough to be delayed, not stale. */
    delayed = 1,
    /** @brief The newest tick is older than the stale threshold. */
    stale = 2,
    /** @brief The plot has no observation to judge. */
    unavailable = 3,
};

/** @brief The ages at which a feed moves from live to delayed to stale. */
struct health_thresholds final {
    std::int64_t live_ns = 5'000'000'000;
    std::int64_t delayed_ns = 30'000'000'000;
};

/** @brief The thresholds for tick-driven FX series. */
inline constexpr health_thresholds fx_health_thresholds{
    .live_ns = 5'000'000'000,
    .delayed_ns = 30'000'000'000,
};

/** @brief The health of a plot, and the age it was judged on. */
struct data_health_state final {
    data_health state = data_health::unavailable;
    std::int64_t age_ns = 0;
};

/**
 * @brief Judges a plot's freshness from its newest observation.
 *
 * A future observation is treated as age zero: a synthetic feed whose clock is
 * a second ahead must read live, not as a negative age. No observation at all
 * is unavailable, which is a different thing from stale.
 */
[[nodiscard]] inline data_health_state assess_data_health(
    std::int64_t observed_at_ns, std::int64_t now_ns,
    health_thresholds thresholds = fx_health_thresholds) {
    if (observed_at_ns <= 0)
        return {.state = data_health::unavailable, .age_ns = 0};
    const auto age = now_ns > observed_at_ns ? now_ns - observed_at_ns : 0;
    if (age <= thresholds.live_ns)
        return {.state = data_health::live, .age_ns = age};
    if (age <= thresholds.delayed_ns)
        return {.state = data_health::delayed, .age_ns = age};
    return {.state = data_health::stale, .age_ns = age};
}

/** @brief A short age, in seconds, minutes, hours or days. */
[[nodiscard]] inline std::string age_text(std::int64_t age_ns) {
    const auto seconds = age_ns / 1'000'000'000;
    if (seconds < 60)
        return std::format("{}s", seconds);
    if (seconds < 3600)
        return std::format("{}m", seconds / 60);
    if (seconds < 86'400)
        return std::format("{}h", seconds / 3600);
    return std::format("{}d", seconds / 86'400);
}

}

#endif
