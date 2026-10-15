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
#include <cstdint>
#include <memory>
#include <utility>

namespace hedgr::model::state {

account_handle account_table::upsert(domain::account row) {
    if (const auto found = by_id_.find(row.id); found != by_id_.end()) {
        rows_[found->second.index] = std::move(row);
        return found->second;
    }

    const auto index = layout_.allocate();
    if (rows_.size() < layout_.capacity())
        rows_.resize(layout_.capacity());

    const account_handle h{index, layout_.generation[index]};
    by_id_.emplace(row.id, h);
    rows_[index] = std::move(row);
    return h;
}

void account_table::remove(account_handle h) {
    if (!contains(h))
        return;
    by_id_.erase(rows_[h.index].id);

    // The value goes with the slot: a hole holds nothing a caller could reach.
    rows_[h.index] = domain::account{};
    layout_.release(h.index);
}

bool account_table::contains(account_handle h) const noexcept {
    return owns(layout_, h);
}

std::size_t account_table::size() const noexcept {
    return layout_.live_count;
}

std::optional<account_handle> account_table::find(std::string_view id) const {
    const auto it = by_id_.find(std::string{id});
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

const domain::account* account_table::get(account_handle h) const noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

domain::account* account_table::get(account_handle h) noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

std::vector<const domain::account*>
account_table::children_of(std::string_view instance_id) const {
    std::vector<const domain::account*> result;
    for (std::uint32_t index = 0; index < layout_.capacity(); ++index) {
        if (!layout_.is_alive(index))
            continue;
        if (rows_[index].instance_id == instance_id)
            result.push_back(std::addressof(rows_[index]));
    }
    return result;
}

void account_table::clear() noexcept {
    layout_ = {};
    rows_.clear();
    by_id_.clear();
}

}
