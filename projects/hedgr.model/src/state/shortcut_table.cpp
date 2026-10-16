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
#include "hedgr.model/state/tables.hpp"
#include <algorithm>
#include <utility>

namespace hedgr::model::state {

namespace {

/**
 * @brief Whether @p left draws before @p right.
 *
 * Position first, which is the operator's order, then id, which is a stable
 * tie-break. Two entries at one position would otherwise put the bar in
 * whatever order the storage happened to return, and a bar that reshuffles
 * itself between reads is worse than one with an arbitrary but fixed order.
 */
[[nodiscard]] bool draws_before(const domain::shortcut& left,
    const domain::shortcut& right) {
    if (left.position != right.position)
        return left.position < right.position;
    return left.id < right.id;
}

}

void shortcut_table::upsert(domain::shortcut row) {
    std::erase_if(rows_, [&row](const domain::shortcut& existing) {
        return existing.id == row.id;
    });

    const auto at = std::lower_bound(rows_.begin(), rows_.end(), row,
        draws_before);
    rows_.insert(at, std::move(row));
}

void shortcut_table::remove(std::string_view id) {
    std::erase_if(rows_, [id](const domain::shortcut& row) {
        return row.id == id;
    });
}

void shortcut_table::remove_target(domain::window_kind kind,
    std::string_view target_id) {
    std::erase_if(rows_, [kind, target_id](const domain::shortcut& row) {
        return row.target_kind == kind && row.target_id == target_id;
    });
}

}
