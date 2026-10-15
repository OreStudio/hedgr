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
#ifndef HEDGR_MODEL_DOMAIN_TIER_HPP
#define HEDGR_MODEL_DOMAIN_TIER_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::model::domain {

/**
 * @brief The safety classification of an instance.
 *
 * Tier is the axis every destructive-action confirmation reads. It is never
 * inferred from an instance's name, only read from stored configuration.
 */
enum class tier : std::uint8_t {
    development = 0,
    staging = 1,
    production = 2,
};

/** @brief The number of tiers, for indexing a lookup table. */
inline constexpr std::size_t tier_count = 3;

/** @brief The canonical text of a tier, as stored in the database and shown. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view to_string(tier value) noexcept;

/** @brief Parses a tier from its canonical text. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<tier, std::string>
tier_from_string(std::string_view text);

}

#endif
