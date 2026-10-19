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

#include <cstdint>
#include <deque>
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

/** @brief One observation, in the deployment's own time and value. */
struct market_point final {
    /** @brief The observation time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value, and the close of a bucket. */
    double value = 0.0;

    /**
     * @brief The term this observation holds for a composite curve, or empty.
     *
     * A leaf series carries its tenor in its identity; a composite curve's
     * observations name the term in their own URI.
     */
    std::string term;

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

/**
 * @brief One series' plotted points and how the last read went.
 *
 * The points and the failure are one value because a screen draws them
 * together: a read that failed must not leave the previous points looking
 * current, and a read that succeeded must clear the last failure.
 */
struct market_series_snapshot final {
    /** @brief One second buckets, two hours. */
    std::deque<market_point> fine;

    /** @brief One minute buckets, two days. */
    std::deque<market_point> medium;

    /** @brief One hour buckets, thirty days. */
    std::deque<market_point> coarse;

    /** @brief When the last read or tick arrived, on the monotonic clock. */
    std::int64_t observed_at_ns = 0;

    /** @brief Why the last read failed, or empty. */
    std::string failure;

    /**
     * @brief Changes whenever anything above changes, and never repeats.
     *
     * The table stamps it from one counter for every chart, so a window that
     * kept plot data derived from a snapshot rebuilds exactly when the stamp
     * it kept differs, including after it switched to another chart.
     */
    std::uint64_t version = 0;

    /** @brief The buckets of one level, oldest first. */
    [[nodiscard]] const std::deque<market_point>&
    level(market_level which) const noexcept {
        switch (which) {
        case market_level::fine:
            return fine;
        case market_level::medium:
            return medium;
        case market_level::coarse:
            return coarse;
        }
        return fine;
    }

    /** @brief Whether this snapshot holds a completed read or a tick. */
    [[nodiscard]] bool is_read() const noexcept {
        return observed_at_ns != 0 && failure.empty();
    }
};

}

#endif
