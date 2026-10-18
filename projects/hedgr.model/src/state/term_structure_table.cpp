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
#include "hedgr.model/state/term_structure_table.hpp"
#include <utility>

namespace hedgr::model::state {

namespace {
constexpr char separator = '\x1f';

[[nodiscard]] std::string key_of(std::string_view instance_id,
    std::string_view account_id, std::string_view curve) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + curve.size() + 2);
    key.append(instance_id);
    key.push_back(separator);
    key.append(account_id);
    key.push_back(separator);
    key.append(curve);
    return key;
}
}

void term_structure_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view key, std::string label) {
    auto& entry = structures_[key_of(instance_id, account_id, key)];
    entry = term_structure{.key = std::string{key}, .label = std::move(label)};
}

void term_structure_table::set(std::string_view instance_id,
    std::string_view account_id, std::string_view key, std::string label,
    std::vector<domain::term_curve> curves) {
    auto& entry = structures_[key_of(instance_id, account_id, key)];
    entry = term_structure{.key = std::string{key},
        .label = std::move(label),
        .curves = std::move(curves)};
}

void term_structure_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view key, std::string message) {
    auto& entry = structures_[key_of(instance_id, account_id, key)];
    entry.key = std::string{key};
    entry.error = std::move(message);
    entry.curves.clear();
}

const term_structure* term_structure_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view key) const {
    const auto found = structures_.find(key_of(instance_id, account_id, key));
    return found == structures_.end() ? nullptr : &found->second;
}

void term_structure_table::clear() {
    structures_.clear();
}

}
