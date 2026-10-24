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
#include <memory>
#include <utility>

namespace hedgr::model::state {

instance_handle instance_table::upsert(domain::instance row) {
    ++version_;
    if (const auto found = by_id_.find(row.id); found != by_id_.end()) {
        rows_[found->second.index] = std::move(row);
        return found->second;
    }

    const auto index = layout_.allocate();
    if (rows_.size() < layout_.capacity())
        rows_.resize(layout_.capacity());

    const instance_handle h{index, layout_.generation[index]};
    by_id_.emplace(row.id, h);
    rows_[index] = std::move(row);
    return h;
}

void instance_table::set_folder(instance_handle h, std::string folder_id) {
    if (!contains(h))
        return;
    ++version_;
    rows_[h.index].folder_id = std::move(folder_id);
}

void instance_table::remove(instance_handle h) {
    if (!contains(h))
        return;
    ++version_;
    by_id_.erase(rows_[h.index].id);

    // The value goes with the slot: a hole holds nothing a caller could reach.
    rows_[h.index] = domain::instance{};
    layout_.release(h.index);
}

bool instance_table::contains(instance_handle h) const noexcept {
    return owns(layout_, h);
}

std::size_t instance_table::size() const noexcept {
    return layout_.live_count;
}

std::optional<instance_handle>
instance_table::find(std::string_view id) const {
    const auto it = by_id_.find(std::string{id});
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

const domain::instance*
instance_table::get(instance_handle h) const noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

domain::instance* instance_table::get(instance_handle h) noexcept {
    if (!contains(h))
        return nullptr;
    ++version_;
    return std::addressof(rows_[h.index]);
}

void instance_table::clear() noexcept {
    ++version_;
    layout_ = {};
    rows_.clear();
    by_id_.clear();
}

}
