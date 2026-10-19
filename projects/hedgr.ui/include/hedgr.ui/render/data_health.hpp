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
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <string_view>

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
    /** @brief The newest observation predates the live feed itself. */
    historical = 4,
};

/** @brief The ages at which a feed moves from live to delayed to stale. */
struct health_thresholds final {
    std::int64_t live_ns;
    std::int64_t delayed_ns;
};

/** @brief The thresholds for tick-driven FX series. */
inline constexpr health_thresholds fx_health_thresholds{
    .live_ns = 5'000'000'000,
    .delayed_ns = 30'000'000'000,
};

/**
 * @brief The thresholds for a term-structure curve.
 *
 * A curve is read on a slow cadence and its pillars are published a little
 * apart, so a two-minute live window keeps a healthy curve from reading as
 * delayed on the frame after every read.
 */
inline constexpr health_thresholds ir_health_thresholds{
    .live_ns = 120'000'000'000,
    .delayed_ns = 900'000'000'000,
};

/**
 * @brief How old a series has to be before it is history rather than stale.
 *
 * Imported vendor series end years ago and are read as history. Calling them
 * stale would make a healthy chart look like a broken feed every time it is
 * opened.
 */
inline constexpr std::int64_t fx_history_ns =
    std::int64_t{10} * 60 * 1'000'000'000;

/** @brief How old a curve has to be before it is history rather than stale. */
inline constexpr std::int64_t ir_history_ns =
    std::int64_t{60} * 60 * 1'000'000'000;

/** @brief The thresholds for a cross-rates batch. */
inline constexpr health_thresholds crm_health_thresholds{
    .live_ns = 300'000'000'000,
    .delayed_ns = 900'000'000'000,
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
    health_thresholds thresholds = fx_health_thresholds,
    std::int64_t history_ns = 0) {
    if (observed_at_ns <= 0)
        return {.state = data_health::unavailable, .age_ns = 0};
    const auto age = now_ns > observed_at_ns ? now_ns - observed_at_ns : 0;
    if (age <= thresholds.live_ns)
        return {.state = data_health::live, .age_ns = age};
    if (age <= thresholds.delayed_ns)
        return {.state = data_health::delayed, .age_ns = age};
    if (history_ns > 0 && age > history_ns)
        return {.state = data_health::historical, .age_ns = age};
    return {.state = data_health::stale, .age_ns = age};
}

/** @brief The word a freshness state carries on every market-data screen. */
[[nodiscard]] inline std::string_view data_health_word(data_health state) {
    switch (state) {
    case data_health::live:
        return "LIVE";
    case data_health::delayed:
        return "DELAYED";
    case data_health::stale:
        return "STALE";
    case data_health::unavailable:
        return "NO DATA";
    case data_health::historical:
        return "HISTORICAL";
    }
    return "NO DATA";
}

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
 * screen can judge and draw the same value instead of parsing it twice.
 */
[[nodiscard]] inline std::int64_t parse_iso8601_utc(std::string_view text) {
    const std::string value{text};
    std::tm utc{};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month,
            &day, &hour, &minute, &second)
        != 6)
        return 0;
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    const auto seconds = timegm(&utc);
    return seconds > 0
        ? static_cast<std::int64_t>(seconds) * 1'000'000'000
        : 0;
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
