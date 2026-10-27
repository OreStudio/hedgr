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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.ui/export.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
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

/**
 * @brief The catalogue series that carries the pair @p base / @p quote.
 *
 * An FX series of @p party_id whose URI names the base as its path and the
 * quote as its @c ccy field. A pair the CRM derives by reciprocal has no series
 * of its own and finds none here. When a pair is listed by more than one
 * provider, a row whose provider is not in @p reference_providers wins over a
 * reference one, which defines the instrument and never ticks. Returns nullptr
 * when nothing matches.
 */
[[nodiscard]] HEDGR_UI_EXPORT const model::domain::market_series_row*
crm_series_for_pair(
    const std::vector<model::domain::market_series_row>& series,
    std::string_view party_id, std::string_view base, std::string_view quote,
    std::span<const std::string> reference_providers = {});

/** @brief One series the matrix's pairs open as a card. */
struct crm_card final {
    /** @brief The series' key. */
    std::string id;

    /** @brief The series' label, for the window's title. */
    std::string label;
};

/**
 * @brief The series behind every pair of the matrix, each once.
 *
 * Walks each unordered pair of the matrix once, in row-major order, and takes
 * the series of either direction, so a pair quoted both ways gives two cards.
 * A pair with no series in either direction is skipped and counted in
 * @p unmatched, and a series is returned once.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::vector<crm_card> crm_pair_cards(
    const std::vector<model::domain::market_series_row>& series,
    std::string_view party_id, const std::vector<std::string>& currencies,
    std::size_t& unmatched,
    std::span<const std::string> reference_providers = {});

/**
 * @brief The most cards "Mini plots" opens without asking first.
 *
 * Thirty floating tiles at the card size fill a desk, so a larger set waits for
 * a confirmation.
 */
inline constexpr std::size_t crm_mini_plot_confirm_above = 30;

}

#endif
