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
#ifndef HEDGR_MODEL_DOMAIN_LABEL_HPP
#define HEDGR_MODEL_DOMAIN_LABEL_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::model::domain {

/** @brief The palette a label is drawn in. */
enum class label_colour : std::uint8_t {
    /** @brief Neutral, for a label that says nothing about state. */
    grey = 0,
    /** @brief Informational. */
    blue = 1,
    /** @brief Healthy. */
    green = 2,
    /** @brief Attention, short of a warning. */
    amber = 3,
    /** @brief Broken or unavailable. */
    red = 4,
    /** @brief Exceptional, for a label outside the usual run. */
    purple = 5,
};

/** @brief The canonical text of a colour, as stored in the local database. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(label_colour value) noexcept;

/** @brief Parses a colour from its canonical text. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<label_colour, std::string>
label_colour_from_string(std::string_view text);

/**
 * @brief How a label affects destructive-action confirmation.
 *
 * Ordered, and the order is the whole point: a higher value is the more
 * serious one, so combining two severities is taking the maximum.
 */
enum class label_severity : std::uint8_t {
    /** @brief No effect on confirmation. */
    none = 0,
    /** @brief The confirmation says so, but proceeds on a click. */
    caution = 1,
    /** @brief The confirmation states the danger and asks for a deliberate
     * second step. */
    danger = 2,
};

/** @brief The canonical text of a severity, as stored in the local database. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(label_severity value) noexcept;

/** @brief Parses a severity from its canonical text. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<label_severity, std::string>
label_severity_from_string(std::string_view text);

/**
 * @brief The higher of two severities.
 *
 * An instance takes the combined severity of its labels and the
 * destructive-action confirmation reads that, which is how a production warning
 * survives without a fixed set of tiers: nothing classifies the instance, the
 * labels simply carry their own severity and the worst one wins. Combining is
 * therefore associative and commutative, and folding a label set with this is
 * order-independent.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT label_severity
combine(label_severity left, label_severity right) noexcept;

/**
 * @brief A free-text tag attached to an instance or an account.
 *
 * A label is an entity rather than a string on the row it tags, so the same
 * label can be attached to many rows and so its colour and severity are
 * declared once. A label is created the first time it is used and is attached
 * by a @c labelling junction; a free-text field beside it would be a second way
 * to say the same thing.
 */
struct label final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief Shown text, unique in the local store. */
    std::string name;

    /** @brief The palette entry the name is drawn in. */
    label_colour colour = label_colour::grey;

    /** @brief How the label affects destructive-action confirmation. */
    label_severity severity = label_severity::none;
};

}

#endif
