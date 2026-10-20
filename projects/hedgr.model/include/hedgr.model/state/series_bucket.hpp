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
#ifndef HEDGR_MODEL_STATE_SERIES_BUCKET_HPP
#define HEDGR_MODEL_STATE_SERIES_BUCKET_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/state/handle.hpp"
#include <cstdint>
#include <type_traits>

namespace hedgr::model::state {

/**
 * @brief One closed bucket on its way from the subscription to the table.
 *
 * Flat and trivially copyable, so publishing one allocates nothing. The series
 * is named by a handle: a watch that is released while a bucket is in flight
 * leaves a handle whose generation no longer matches, and the table rejects the
 * bucket instead of writing it into the next occupant of the slot.
 */
struct series_bucket final {
    /** @brief The series the bucket belongs to. */
    series_handle series;

    /** @brief The bucket's time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The close. */
    double value = 0.0;

    /** @brief The first value in the bucket. */
    double open = 0.0;

    /** @brief The highest value in the bucket. */
    double high = 0.0;

    /** @brief The lowest value in the bucket. */
    double low = 0.0;

    /** @brief The level the bucket closes. */
    domain::market_level level = domain::market_level::fine;
};

static_assert(std::is_trivially_copyable_v<series_bucket>);
static_assert(sizeof(series_bucket) == 56);

}

#endif
