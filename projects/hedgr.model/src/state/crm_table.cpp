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
[[nodiscard]] key_parts<5> key_of(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) {
    return {instance_id, account_id, party_id, crm_name,
        reciprocal ? std::string_view{"1"} : std::string_view{"0"}};
}
}

void crm_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) {
    auto& entry = slot_in(snapshots_, key_of(instance_id, account_id, party_id,
        crm_name, reciprocal));
    entry.reading = true;
    entry.error.clear();
}

void crm_table::set(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal,
    std::vector<domain::crm_cell> cells,
    std::vector<domain::crm_convention> conventions,
    std::int64_t observed_as_of_ns, std::int64_t latency_ns) {
    auto& entry = slot_in(snapshots_, key_of(instance_id, account_id, party_id,
        crm_name, reciprocal));
    entry.cells = std::move(cells);
    entry.conventions = std::move(conventions);
    entry.observed_as_of_ns = observed_as_of_ns;
    entry.latency_ns = latency_ns;
    entry.reading = false;
    entry.error.clear();
    entry.version = ++version_;
}

void crm_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal, std::string message) {
    auto& entry = slot_in(snapshots_, key_of(instance_id, account_id, party_id,
        crm_name, reciprocal));
    entry.reading = false;
    entry.error = std::move(message);
}

void crm_table::schedule(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal, std::int64_t next_read_ns) {
    slot_in(snapshots_, key_of(instance_id, account_id, party_id, crm_name,
        reciprocal)).next_read_ns = next_read_ns;
}

const crm_snapshot* crm_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::string_view crm_name, bool reciprocal) const {
    return find_in(snapshots_,
        key_of(instance_id, account_id, party_id, crm_name, reciprocal));
}

void crm_table::set_names(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id,
    std::vector<std::string> names) {
    slot_in(names_, {instance_id, account_id, party_id}) = std::move(names);
}

const std::vector<std::string>* crm_table::names(
    std::string_view instance_id, std::string_view account_id,
    std::string_view party_id) const {
    return find_in(names_, {instance_id, account_id, party_id});
}

void crm_table::clear() {
    snapshots_.clear();
    names_.clear();
}

}
