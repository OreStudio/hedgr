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
#include <string>
#include <utility>

namespace hedgr::model::state {

namespace {

/**
 * @brief Whether @p row attaches @p label_id to the owner.
 *
 * The pair is the identity of an attachment, so this is the one comparison
 * every operation here is built from: add uses it to stay idempotent, remove
 * uses it to find the row to drop, and labels_of uses it to collect a list.
 */
[[nodiscard]] bool matches(const domain::labelling& row,
    domain::labelling_owner owner, std::string_view owner_id,
    std::string_view label_id) {
    return row.owner == owner && row.owner_id == owner_id
        && row.label_id == label_id;
}

}

void labelling_table::add(domain::labelling_owner owner, std::string owner_id,
    std::string label_id) {
    const auto existing = std::find_if(rows_.begin(), rows_.end(),
        [&](const domain::labelling& row) {
            return matches(row, owner, owner_id, label_id);
        });
    if (existing != rows_.end())
        return;

    rows_.push_back(domain::labelling{owner, std::move(owner_id),
        std::move(label_id)});
}

void labelling_table::remove(domain::labelling_owner owner,
    std::string owner_id, std::string label_id) {
    const auto found = std::find_if(rows_.begin(), rows_.end(),
        [&](const domain::labelling& row) {
            return matches(row, owner, owner_id, label_id);
        });
    if (found == rows_.end())
        return;

    /*
     * The insertion order of the remaining attachments is what labels_of
     * reports, so the row is erased in place rather than swapped with the last
     * one, which would reorder the list a caller is reading.
     */
    rows_.erase(found);
}

void labelling_table::remove_owner(domain::labelling_owner owner,
    std::string_view owner_id) {
    std::erase_if(rows_, [&](const domain::labelling& row) {
        return row.owner == owner && row.owner_id == owner_id;
    });
}

std::vector<std::string> labelling_table::labels_of(
    domain::labelling_owner owner, std::string_view owner_id) const {
    std::vector<std::string> result;
    for (const auto& row : rows_) {
        if (row.owner == owner && row.owner_id == owner_id)
            result.push_back(row.label_id);
    }
    return result;
}

}
