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
#ifndef HEDGR_UI_RENDER_TERM_LADDER_HPP
#define HEDGR_UI_RENDER_TERM_LADDER_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::render {

/** @brief The days a tenor spells, so tenors sort by length of time. */
[[nodiscard]] HEDGR_UI_EXPORT int tenor_rank(std::string_view tenor);

/** @brief Everything a term structure's ladder is derived from. */
struct term_ladder_key final {
    /** @brief The term structure row's stamp, or 0 when nothing is cached. */
    std::uint64_t version = 0;
    int history = 0;

    [[nodiscard]] bool operator==(const term_ladder_key&) const = default;
};

/** @brief One tenor on the ladder, with its rank computed once. */
struct term_rung final {
    int rank = 0;
    std::string tenor;
};

/**
 * @brief What a term structure window derives from its curves, kept between
 * frames.
 *
 * A read arrives about every thirty seconds and a frame sixty times a second,
 * so the sorted ladder and the value of every overlay at every rung are
 * rebuilt only when the key changes. The label pointers point into @c rungs,
 * so a copy starts empty and rebuilds instead of keeping them.
 */
struct term_ladder final {
    term_ladder() = default;
    term_ladder(const term_ladder&) noexcept {}
    term_ladder& operator=(const term_ladder&) noexcept {
        *this = term_ladder{};
        return *this;
    }
    term_ladder(term_ladder&&) noexcept = default;
    term_ladder& operator=(term_ladder&&) noexcept = default;

    term_ladder_key key;

    /** @brief The newest complete curve's tenors, ordered by rank. */
    std::vector<term_rung> rungs;

    /** @brief The position of each rung, as the axis wants it. */
    std::vector<double> xs;

    /** @brief The axis labels, pointing into @c rungs. */
    std::vector<const char*> labels;

    /** @brief Positions in @c rungs ordered by tenor and then position. */
    std::vector<std::uint32_t> by_tenor;

    /** @brief One row per overlay, oldest first, NaN where a tenor is absent. */
    std::vector<std::vector<double>> ys;

    /** @brief How many times it has been rebuilt. */
    std::size_t builds = 0;

    /** @brief The position of a tenor on the ladder, or @c rungs.size(). */
    [[nodiscard]] HEDGR_UI_EXPORT std::size_t position_of(std::string_view tenor) const;
};

/**
 * @brief Rebuilds @p ladder when @p key differs from its own.
 *
 * The ladder is the nodes of @p curves[@p primary] and the overlays are
 * @p curves[@p first] up to and including the primary.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_term_ladder(term_ladder& ladder,
    const term_ladder_key& key,
    const std::vector<hedgr::model::domain::term_curve>& curves,
    std::size_t first, std::size_t primary);

}

#endif
