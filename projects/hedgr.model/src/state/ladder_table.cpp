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
#include "hedgr.model/state/ladder_table.hpp"

#include <format>
#include <utility>

namespace hedgr::model::state {

std::string ladder_key(std::string_view currency,
    std::string_view instrument_type, std::string_view forward_start) {
    return std::format("{}\x1f{}\x1f{}", currency, instrument_type,
        forward_start);
}

void ladder_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view key) {
    auto& row = slot_in(rows_, {instance_id, account_id, key});
    row.state = ladder_state::reading;
    row.error.clear();
    row.version = ++version_;
}

void ladder_table::apply(std::string_view instance_id,
    std::string_view account_id, std::string_view key,
    domain::rate_ladder ladder) {
    auto& row = slot_in(rows_, {instance_id, account_id, key});
    row.ladder = std::move(ladder);
    row.error.clear();
    row.state = ladder_state::ready;
    row.version = ++version_;
}

void ladder_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view key, std::string message) {
    auto& row = slot_in(rows_, {instance_id, account_id, key});
    row.error = std::move(message);
    row.state = ladder_state::failed;
    row.version = ++version_;
}

const ladder_row* ladder_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view key) const {
    const auto* found = find_in(rows_, {instance_id, account_id, key});
    return found != nullptr && found->version != 0 ? found : nullptr;
}

void ladder_table::clear() {
    rows_.clear();
}

}
