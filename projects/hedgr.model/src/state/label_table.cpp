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

label_handle label_table::upsert(domain::label row) {
    if (const auto found = by_id_.find(row.id); found != by_id_.end()) {
        /*
         * A rename has to move the name index too, and the old name has to go
         * first: the row is about to be overwritten, so this is the last moment
         * the name it used to carry is readable.
         */
        const auto previous = by_name_.find(rows_[found->second.index].name);
        if (previous != by_name_.end() && previous->second == found->second)
            by_name_.erase(previous);

        by_name_[row.name] = found->second;
        rows_[found->second.index] = std::move(row);
        return found->second;
    }

    const auto index = layout_.allocate();
    if (rows_.size() < layout_.capacity())
        rows_.resize(layout_.capacity());

    const label_handle h{index, layout_.generation[index]};
    by_id_.emplace(row.id, h);
    by_name_[row.name] = h;
    rows_[index] = std::move(row);
    return h;
}

void label_table::remove(label_handle h) {
    if (!contains(h))
        return;
    by_id_.erase(rows_[h.index].id);
    by_name_.erase(rows_[h.index].name);

    // The value goes with the slot: a hole holds nothing a caller could reach.
    rows_[h.index] = domain::label{};
    layout_.release(h.index);
}

bool label_table::contains(label_handle h) const noexcept {
    return owns(layout_, h);
}

std::size_t label_table::size() const noexcept {
    return layout_.live_count;
}

std::optional<label_handle> label_table::find(std::string_view id) const {
    const auto it = by_id_.find(std::string{id});
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

std::optional<label_handle>
label_table::find_by_name(std::string_view name) const {
    const auto it = by_name_.find(std::string{name});
    if (it == by_name_.end())
        return std::nullopt;
    return it->second;
}

const domain::label* label_table::get(label_handle h) const noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

domain::label* label_table::get(label_handle h) noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

void label_table::clear() noexcept {
    layout_ = {};
    rows_.clear();
    by_id_.clear();
    by_name_.clear();
}

}
