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
#ifndef HEDGR_UI_RENDER_CRM_GRID_HPP
#define HEDGR_UI_RENDER_CRM_GRID_HPP

#include "hedgr.model/domain/health.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.ui/export.hpp"
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::render {

/** @brief A delta this small rounds to 0.00% at two decimal places. */
inline constexpr double crm_zero_delta = 0.005;

/** @brief Whether a delta should print as a neutral zero. */
[[nodiscard]] inline bool crm_delta_is_zero(double percent) {
    return std::abs(percent) < crm_zero_delta;
}

/** @brief One base/quote cell of the matrix, derived once per batch. */
struct crm_grid_cell final {
    /** @brief The batch cell drawn here, or nullptr when the pair has none. */
    const model::domain::crm_cell* cell = nullptr;

    /** @brief The pair, for example EUR/USD. */
    std::string pair;

    /** @brief The rate under the pair's display convention. */
    std::string rate;

    /** @brief The selectable's text: the rate, with the pair as its id. */
    std::string label;
};

/**
 * @brief A CRM batch as the matrix draws it.
 *
 * Rebuilt only when the batch's version moves, so a steady frame formats and
 * allocates nothing per cell. The cell pointers are valid for that version.
 */
struct crm_grid final {
    /** @brief The batch version this was built from; zero before any. */
    std::uint64_t version = 0;

    /** @brief The rows and the columns, sorted. */
    std::vector<std::string> currencies;

    /** @brief Row-major, currencies.size() squared. */
    std::vector<crm_grid_cell> cells;

    /** @brief The deployment's verdicts over the cells drawn. */
    model::domain::verdict_counts counts;
};

/**
 * @brief Derives the matrix for one CRM from a batch.
 *
 * One cell per pair, preferring a usable rate over an unavailable one, and the
 * counts are taken from those same cells so the headline agrees with the grid.
 * @p seen keeps every currency shown so far, so the grid does not shrink when
 * one batch omits a pair. Rates are formatted by OreStudio's
 * currency_pair_rate_formatter, looking the convention up in either direction.
 */
[[nodiscard]] HEDGR_UI_EXPORT crm_grid build_crm_grid(
    const model::state::crm_snapshot& batch, std::string_view crm_name,
    std::set<std::string>& seen);

}

#endif
