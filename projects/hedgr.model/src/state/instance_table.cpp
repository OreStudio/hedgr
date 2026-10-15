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

namespace hedgr::model::state {

const domain::instance*
instance_table::find(std::string_view id) const noexcept {
    const auto it = std::ranges::find(rows_, id, &domain::instance::id);
    return it == rows_.end() ? nullptr : std::addressof(*it);
}

bool instance_table::upsert(domain::instance row) {
    const auto it = std::ranges::find(rows_, row.id, &domain::instance::id);
    if (it == rows_.end()) {
        rows_.push_back(std::move(row));
        return false;
    }
    *it = std::move(row);
    return true;
}

bool instance_table::remove(std::string_view id) {
    const auto it = std::ranges::find(rows_, id, &domain::instance::id);
    if (it == rows_.end())
        return false;
    rows_.erase(it);
    return true;
}

}
