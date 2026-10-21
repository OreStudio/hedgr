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
#ifndef HEDGR_MODEL_DOMAIN_MARKET_HPP
#define HEDGR_MODEL_DOMAIN_MARKET_HPP

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace hedgr::model::domain {

/**
 * @brief One market series, as the catalogue lists it.
 *
 * The URI is the identity every request carries. The label is the interface's
 * reading of it, made once when the catalogue arrives.
 */
struct market_series_row final {
    /** @brief The series' own key. */
    std::string id;

    /** @brief The canonical oresmd URI, the series' identity. */
    std::string uri;

    /** @brief A person's name for the series. */
    std::string label;

    /** @brief The asset class, upper-cased. */
    std::string asset_class;

    /** @brief Who produced the series, for example SYNTHETIC. */
    std::string producer_kind;

    /** @brief The party that owns the series. */
    std::string party_id;

    /** @brief The tenor or term the series carries, or empty. */
    std::string tenor;

    /** @brief The curve identity the series belongs to, or empty. */
    std::string curve_key;

    /** @brief The curve's name, from the URI without its term. */
    std::string curve_label;
};

/** @brief One node of a term structure: a tenor and the rate at it. */
struct term_node final {
    /** @brief The tenor or term label, as the series URI spells it. */
    std::string tenor;

    /** @brief The rate at that tenor. */
    double value = 0.0;

    /** @brief The observation time of the value. */
    std::int64_t at_ns = 0;
};

/** @brief One observation of a term structure at one instant. */
struct term_curve final {
    /** @brief The instant the curve was observed. */
    std::int64_t at_ns = 0;

    /** @brief One value per tenor. */
    std::vector<term_node> nodes;
};

/** @brief The three resolutions a live series keeps. */
enum class market_level : std::uint8_t {
    fine = 0,
    medium = 1,
    coarse = 2,
};

/** @brief The interval one fine bucket covers. */
inline constexpr std::int64_t market_fine_ns = 1'000'000'000;

/** @brief The interval one medium bucket covers. */
inline constexpr std::int64_t market_medium_ns = 60 * market_fine_ns;

/** @brief The interval one coarse bucket covers. */
inline constexpr std::int64_t market_coarse_ns = 60 * market_medium_ns;

/** @brief How many fine buckets a series keeps, two hours at one second. */
inline constexpr std::size_t market_fine_capacity = 7200;

/** @brief How many medium buckets a series keeps, two days at one minute. */
inline constexpr std::size_t market_medium_capacity = 2880;

/** @brief How many coarse buckets a series keeps, thirty days at one hour. */
inline constexpr std::size_t market_coarse_capacity = 720;

/** @brief The interval a level aggregates over. */
[[nodiscard]] inline constexpr std::int64_t
market_level_ns(market_level level) noexcept {
    switch (level) {
    case market_level::fine:
        return market_fine_ns;
    case market_level::medium:
        return market_medium_ns;
    case market_level::coarse:
        return market_coarse_ns;
    }
    return market_fine_ns;
}

/** @brief The number of buckets a level keeps. */
[[nodiscard]] inline constexpr std::size_t
market_level_capacity(market_level level) noexcept {
    switch (level) {
    case market_level::fine:
        return market_fine_capacity;
    case market_level::medium:
        return market_medium_capacity;
    case market_level::coarse:
        return market_coarse_capacity;
    }
    return market_fine_capacity;
}

/**
 * @brief The level a chart span reads from.
 *
 * Up to two hours reads the fine level, up to two days the medium level, and
 * anything longer, or the whole series (zero), the coarse level.
 */
[[nodiscard]] inline constexpr market_level
market_level_for_span(std::int64_t span_ns) noexcept {
    if (span_ns == 0)
        return market_level::coarse;
    if (span_ns <= 2 * market_coarse_ns)
        return market_level::fine;
    if (span_ns <= 2 * 24 * market_coarse_ns)
        return market_level::medium;
    return market_level::coarse;
}

/** @brief Observations one history page asks for. */
inline constexpr std::size_t market_history_page = 2000;

/**
 * @brief The most pages one history read fetches.
 *
 * Bounds a read of a dense series: 20 pages is 40,000 observations, a few
 * megabytes in flight, and each page stays the size the first backfill already
 * proves the deployment answers.
 */
inline constexpr std::size_t market_history_max_pages = 20;

/** @brief What a deeper read of a series would ask the deployment for. */
struct history_plan final {
    /** @brief The level the span reads from. */
    market_level level = market_level::fine;

    /** @brief The earliest instant the span needs, in epoch nanoseconds. */
    std::int64_t from_ns = 0;

    /** @brief The instant the read stops at, the level's oldest held bucket. */
    std::int64_t to_ns = 0;
};

/**
 * @brief The read a chart span needs beyond what the series holds, if any.
 *
 * A span is measured back from the newest point of its level, as the plot
 * crops it, and never reaches further than the level's ring keeps. Nothing is
 * needed when the level already holds a bucket at or before the start of the
 * span, when an earlier read already asked at least that far back (so a series
 * the deployment holds little of is not asked again), or when the level holds
 * nothing to extend.
 *
 * @param span_ns the chart span, or zero for the whole series.
 * @param level_oldest_ns the oldest held bucket of the span's level, or zero.
 * @param level_newest_ns the newest held bucket of that level, or zero.
 * @param floor_ns how far back an earlier read asked, or zero for none.
 */
[[nodiscard]] inline constexpr std::optional<history_plan>
plan_history_read(std::int64_t span_ns, std::int64_t level_oldest_ns,
    std::int64_t level_newest_ns, std::int64_t floor_ns) noexcept {
    if (level_oldest_ns == 0 || level_newest_ns == 0)
        return std::nullopt;
    const auto level = market_level_for_span(span_ns);
    const auto bucket = market_level_ns(level);
    const auto kept = static_cast<std::int64_t>(market_level_capacity(level))
        * bucket;
    const auto wanted = span_ns == 0 ? kept : std::min(span_ns, kept);
    const auto from_ns = level_newest_ns - wanted;
    if (level_oldest_ns <= from_ns + bucket)
        return std::nullopt;
    if (floor_ns != 0 && floor_ns <= from_ns)
        return std::nullopt;
    return history_plan{.level = level, .from_ns = from_ns,
        .to_ns = level_oldest_ns};
}

/** @brief One observation, in the deployment's own time and value. */
struct market_point final {
    /** @brief The observation time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value, and the close of a bucket. */
    double value = 0.0;

    /**
     * @brief The bucket's first value.
     *
     * Zero on a point that was not built from a bucket, which a candle reads
     * as "use the value for all four prices". Live aggregation and backfill
     * fill all of open, high and low.
     */
    double open = 0.0;

    /** @brief The highest value in the bucket. */
    double high = 0.0;

    /** @brief The lowest value in the bucket. */
    double low = 0.0;
};

}

#endif
