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
#include <format>
#include <memory>
#include <utility>

namespace hedgr::model::state {

std::expected<folder_handle, std::string>
folder_table::upsert(domain::folder row) {
    /*
     * A folder may not be its own ancestor. The walk in the two methods below
     * follows parent_id upwards, so a cycle would make it run forever, and the
     * refusal is the cheapest place to guarantee that cannot happen. The check
     * runs before the id is written, so a refused move leaves the row as it
     * was.
     */
    if (is_ancestor_of(row.id, row.parent_id)) {
        return std::unexpected(std::format(
            "cannot parent folder {} to {}: it would contain itself",
            row.id, row.parent_id));
    }

    if (const auto found = by_id_.find(row.id); found != by_id_.end()) {
        rows_[found->second.index] = std::move(row);
        return found->second;
    }

    const auto index = layout_.allocate();
    if (rows_.size() < layout_.capacity())
        rows_.resize(layout_.capacity());

    const folder_handle h{index, layout_.generation[index]};
    by_id_.emplace(row.id, h);
    rows_[index] = std::move(row);
    return h;
}

void folder_table::remove(folder_handle h) {
    if (!contains(h))
        return;
    by_id_.erase(rows_[h.index].id);

    // The value goes with the slot: a hole holds nothing a caller could reach.
    rows_[h.index] = domain::folder{};
    layout_.release(h.index);
}

bool folder_table::contains(folder_handle h) const noexcept {
    return owns(layout_, h);
}

std::size_t folder_table::size() const noexcept {
    return layout_.live_count;
}

std::optional<folder_handle>
folder_table::find(std::string_view id) const {
    const auto it = by_id_.find(std::string{id});
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

const domain::folder* folder_table::get(folder_handle h) const noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

domain::folder* folder_table::get(folder_handle h) noexcept {
    if (!contains(h))
        return nullptr;
    return std::addressof(rows_[h.index]);
}

std::vector<const domain::folder*>
folder_table::children_of(std::string_view parent_id) const {
    std::vector<const domain::folder*> result;
    for (std::uint32_t index = 0; index < layout_.capacity(); ++index) {
        if (!layout_.is_alive(index))
            continue;
        if (rows_[index].parent_id == parent_id)
            result.push_back(std::addressof(rows_[index]));
    }
    return result;
}

std::optional<folder_handle>
folder_table::root(domain::folder_tree tree, std::string_view owner_id) const {
    for (std::uint32_t index = 0; index < layout_.capacity(); ++index) {
        if (!layout_.is_alive(index))
            continue;
        const auto& row = rows_[index];
        if (row.tree != tree || row.owner_id != owner_id)
            continue;
        if (row.parent_id.empty())
            return folder_handle{index, layout_.generation[index]};
    }
    return std::nullopt;
}

std::vector<std::string>
folder_table::descendants_of(std::string_view folder_id) const {
    std::vector<std::string> result;
    std::vector<std::string> frontier{std::string{folder_id}};

    /*
     * Breadth first with a queue rather than recursion: the frontier is the
     * folders whose children have not been listed yet, and expanding it level by
     * level is what makes the result a walk of the tree rather than of the slab.
     */
    while (!frontier.empty()) {
        const auto parent = std::move(frontier.back());
        frontier.pop_back();
        for (const auto* child : children_of(parent)) {
            result.push_back(child->id);
            frontier.push_back(child->id);
        }
    }
    return result;
}

std::vector<std::string>
folder_table::ancestors_of(std::string_view folder_id) const {
    std::vector<std::string> result;
    const auto start = find(folder_id);
    if (!start)
        return result;

    std::string current = get(*start)->parent_id;
    while (!current.empty()) {
        result.push_back(current);
        const auto handle = find(current);
        if (!handle)
            break;
        current = get(*handle)->parent_id;
    }
    return result;
}

bool folder_table::is_ancestor_of(std::string_view candidate,
    std::string_view id) const {
    if (candidate.empty() || id.empty())
        return false;
    if (candidate == id)
        return true;

    const auto ancestors = ancestors_of(id);
    return std::find(ancestors.begin(), ancestors.end(), candidate)
        != ancestors.end();
}

void folder_table::clear() noexcept {
    layout_ = {};
    rows_.clear();
    by_id_.clear();
}

}
