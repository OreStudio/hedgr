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
#ifndef HEDGR_MODEL_DOMAIN_CRM_HPP
#define HEDGR_MODEL_DOMAIN_CRM_HPP

#include <optional>
#include <string>

namespace hedgr::model::domain {

/** @brief Display convention for one currency pair's spot rate. */
struct crm_convention final {
    /** @brief The pair code, for example EUR/USD. */
    std::string pair_code;

    /** @brief Decimal places a spot rate for this pair is shown with. */
    int decimal_places = 5;

    /** @brief Absolute size of a pip, for example 0.0001. */
    double pip_factor = 0.0001;

    /** @brief Minimum tick, in pips. */
    double tick_size = 1.0;
};

/** @brief One cell of a cross-rates matrix, as the batch read returned it. */
struct crm_cell final {
    /** @brief The CRM that produced this rate. */
    std::string crm_name;

    /** @brief The base currency of the pair. */
    std::string base_currency_code;

    /** @brief The quote currency of the pair. */
    std::string quote_currency_code;

    /** @brief The rate, as displayed (post-reciprocal when that applies). */
    double rate = 0.0;

    /** @brief fresh, stale or unavailable. */
    std::string status;

    /** @brief The observation time, ISO-8601 UTC; empty when unavailable. */
    std::string as_of;

    /** @brief Whether the rate is 1/reverse rather than a direct quote. */
    bool reciprocal = false;

    /** @brief Change against the last value served for this pair. */
    std::optional<double> delta_pct;
};

}

#endif
