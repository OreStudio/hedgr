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
#ifndef HEDGR_MODEL_DOMAIN_HEALTH_HPP
#define HEDGR_MODEL_DOMAIN_HEALTH_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hedgr::model::domain {

/** @brief How fresh the data behind a market window is. */
enum class data_health : std::uint8_t {
    /** @brief A tick arrived within the live window. */
    live = 0,
    /** @brief The newest tick is old enough to be delayed, not stale. */
    delayed = 1,
    /** @brief The newest tick is older than the stale threshold. */
    stale = 2,
    /** @brief There is no observation to judge. */
    unavailable = 3,
};

/** @brief The ages at which a feed moves from live to delayed to stale. */
struct health_thresholds final {
    std::int64_t live_ns;
    std::int64_t delayed_ns;
};

/**
 * @brief The thresholds for tick-driven FX series.
 *
 * A provider publishes on its own cadence and a few seconds of quiet is not
 * news: the stamp is live for half a minute, at ease for five, and only then
 * stale. The first window was five seconds, which turned a feed that ticks
 * every five seconds into a permanent DELAYED badge and taught the operator to
 * ignore the one signal that says the feed has stopped.
 */
inline constexpr health_thresholds fx_health_thresholds{
    .live_ns = 30'000'000'000,
    .delayed_ns = 300'000'000'000,
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
 * @brief Judges freshness from the newest observation's own timestamp.
 *
 * Both instants are on the wall clock the market stamps its observations
 * with. A future observation is age zero: a synthetic feed whose clock is a
 * second ahead must read live, not as a negative age. No observation at all is
 * unavailable, which is a different thing from stale.
 */
[[nodiscard]] constexpr data_health assess_data_health(
    std::int64_t as_of_ns, std::int64_t now_ns,
    health_thresholds thresholds) noexcept {
    if (as_of_ns <= 0)
        return data_health::unavailable;
    const auto age = now_ns > as_of_ns ? now_ns - as_of_ns : 0;
    if (age <= thresholds.live_ns)
        return data_health::live;
    if (age <= thresholds.delayed_ns)
        return data_health::delayed;
    return data_health::stale;
}

/**
 * @brief How many rates of a batch carry each of the deployment's verdicts.
 *
 * The verdicts are OreStudio's @c rate_status: fresh, stale (lagging but
 * still moving), disconnected (the feed stopped) and unavailable (never
 * ticked).
 */
struct verdict_counts final {
    std::size_t fresh = 0;
    std::size_t stale = 0;
    std::size_t disconnected = 0;
    std::size_t unavailable = 0;
};

/**
 * @brief Judges a batch from the deployment's verdicts on its rates.
 *
 * The server's "stale" means lagging, which is Hedgr's delayed; Hedgr's stale
 * means a dead feed, which the server calls disconnected. A batch with any
 * disconnected rate is stale. One with a lagging or missing rate beside rates
 * that are usable is delayed. A batch with no usable rate at all is
 * unavailable.
 */
[[nodiscard]] constexpr data_health assess_verdicts(
    const verdict_counts& counts) noexcept {
    if (counts.disconnected > 0)
        return data_health::stale;
    if (counts.fresh == 0 && counts.stale == 0)
        return data_health::unavailable;
    if (counts.stale > 0 || counts.unavailable > 0)
        return data_health::delayed;
    return data_health::live;
}

/** @brief The word a freshness state carries on every market-data screen. */
[[nodiscard]] constexpr std::string_view data_health_word(
    data_health state) noexcept {
    switch (state) {
    case data_health::live:
        return "LIVE";
    case data_health::delayed:
        return "DELAYED";
    case data_health::stale:
        return "STALE";
    case data_health::unavailable:
        return "NO DATA";
    }
    return "NO DATA";
}

}

#endif
