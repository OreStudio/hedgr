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
#ifndef HEDGR_MODEL_STATE_TERM_STRUCTURE_TABLE_HPP
#define HEDGR_MODEL_STATE_TERM_STRUCTURE_TABLE_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/composite_key.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::model::state {

/** @brief The latest curve one browser session read. */
struct term_structure final {
    /** @brief The curve's identity, the series URI without its term. */
    std::string key;

    /** @brief The curve's name, for the plot heading. */
    std::string label;

    /** @brief The recent curves, oldest first. */
    std::vector<domain::term_curve> curves;

    /** @brief Whether a read is in flight while the last good curves stay on
     * screen. */
    bool reading = false;

    /** @brief Why the read failed, or empty. */
    std::string error;
};

/**
 * @brief The term structures a session has read, keyed by session and curve.
 *
 * A curve is read only while an operator is looking at it, so it lives beside
 * the catalogue rather than in the chart's point rings. Each completed read
 * replaces the curves it holds, and a read in flight or a read that failed
 * leaves the last good curves in place, so the window never blanks.
 */
class HEDGR_MODEL_EXPORT term_structure_table final {
public:
    /** @brief Marks a curve read as started, keeping the curves on screen. */
    void begin(std::string_view instance_id, std::string_view account_id,
        std::string_view key, std::string label);

    /** @brief Stores a completed curve read. */
    void set(std::string_view instance_id, std::string_view account_id,
        std::string_view key, std::string label,
        std::vector<domain::term_curve> curves);

    /** @brief Records a failed curve read. */
    void fail(std::string_view instance_id, std::string_view account_id,
        std::string_view key, std::string message);

    /** @brief One session's curve, or nullptr. */
    [[nodiscard]] const term_structure* get(std::string_view instance_id,
        std::string_view account_id, std::string_view key) const;

    void clear();

private:
    composite_map<3, term_structure> structures_;
};

}

#endif
