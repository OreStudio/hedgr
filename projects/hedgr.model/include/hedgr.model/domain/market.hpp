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
};

/** @brief One observation, in the deployment's own time and value. */
struct market_point final {
    /** @brief The observation time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value. */
    double value = 0.0;
};

/**
 * @brief One series' plotted points and how the last read went.
 *
 * The points and the failure are one value because a screen draws them
 * together: a read that failed must not leave the previous points looking
 * current, and a read that succeeded must clear the last failure.
 */
struct market_series_snapshot final {
    /** @brief The points, oldest first. */
    std::vector<market_point> points;

    /** @brief When the read finished, or zero when none has. */
    std::int64_t observed_at_ns = 0;

    /** @brief Why the last read failed, or empty. */
    std::string failure;

    /** @brief Whether this snapshot holds a completed read. */
    [[nodiscard]] bool is_read() const noexcept {
        return observed_at_ns != 0 && failure.empty();
    }
};

}

#endif
