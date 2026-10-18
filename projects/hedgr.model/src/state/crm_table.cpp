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
#include "hedgr.model/state/crm_table.hpp"
#include <utility>

namespace hedgr::model::state {

namespace {
constexpr char separator = '\x1f';

[[nodiscard]] std::string key_of(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + party_id.size() + 2);
    key.append(instance_id);
    key.push_back(separator);
    key.append(account_id);
    key.push_back(separator);
    key.append(party_id);
    return key;
}
}

void crm_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id)];
    entry.reading = true;
    entry.error.clear();
}

void crm_table::set(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::vector<domain::crm_cell> cells, std::string observed_as_of) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id)];
    entry.cells = std::move(cells);
    entry.observed_as_of = std::move(observed_as_of);
    entry.reading = false;
    entry.error.clear();
}

void crm_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string message) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id)];
    entry.cells.clear();
    entry.reading = false;
    entry.error = std::move(message);
}

const crm_snapshot* crm_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) const {
    const auto found = snapshots_.find(key_of(instance_id, account_id, party_id));
    return found == snapshots_.end() ? nullptr : &found->second;
}

void crm_table::clear() {
    snapshots_.clear();
}

}
