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
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + party_id.size()
        + crm_name.size() + 4);
    key.append(instance_id);
    key.push_back(separator);
    key.append(account_id);
    key.push_back(separator);
    key.append(party_id);
    key.push_back(separator);
    key.append(crm_name);
    key.push_back(separator);
    key.push_back(reciprocal ? '1' : '0');
    return key;
}
}

void crm_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id,
        crm_name, reciprocal)];
    entry.reading = true;
    entry.error.clear();
}

void crm_table::set(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal,
    std::vector<domain::crm_cell> cells,
    std::vector<domain::crm_convention> conventions,
    std::string observed_as_of, std::int64_t latency_ns) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id,
        crm_name, reciprocal)];
    entry.cells = std::move(cells);
    entry.conventions = std::move(conventions);
    entry.observed_as_of = std::move(observed_as_of);
    entry.latency_ns = latency_ns;
    entry.reading = false;
    entry.error.clear();
}

void crm_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal, std::string message) {
    auto& entry = snapshots_[key_of(instance_id, account_id, party_id,
        crm_name, reciprocal)];
    entry.cells.clear();
    entry.reading = false;
    entry.error = std::move(message);
}

const crm_snapshot* crm_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) const {
    const auto found = snapshots_.find(key_of(instance_id, account_id,
        party_id, crm_name, reciprocal));
    return found == snapshots_.end() ? nullptr : &found->second;
}

void crm_table::set_names(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::vector<std::string> names) {
    names_[key_of(instance_id, account_id, party_id, "names", false)]
        = std::move(names);
}

const std::vector<std::string>* crm_table::names(
    std::string_view instance_id, std::string_view account_id,
    std::string_view party_id) const {
    const auto found = names_.find(key_of(instance_id, account_id, party_id,
        "names", false));
    return found == names_.end() ? nullptr : &found->second;
}

void crm_table::clear() {
    snapshots_.clear();
    names_.clear();
}

}
