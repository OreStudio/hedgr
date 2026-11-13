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
#include "hedgr.model/state/condition_table.hpp"
#include <algorithm>
#include <utility>

namespace hedgr::model::state {

condition_table::condition_table(std::size_t capacity) noexcept
    : capacity_(capacity) {}

void condition_table::raise(condition_key key, condition_severity severity,
    std::string message, std::int64_t now_ns) {
    static_cast<void>(clear(key));
    if (rows_.size() == capacity_)
        rows_.erase(rows_.begin());

    rows_.push_back(condition{
        .key = std::move(key),
        .severity = severity,
        .message = std::move(message),
        .raised_ns = now_ns,
    });
}

bool condition_table::clear(const condition_key& key) {
    const auto it = std::ranges::find(rows_, key, &condition::key);
    if (it == rows_.end())
        return false;
    rows_.erase(it);
    return true;
}

std::size_t condition_table::clear(condition_source source) {
    return static_cast<std::size_t>(std::erase_if(rows_,
        [source](const condition& c) { return c.key.source == source; }));
}

std::size_t condition_table::clear_verdicts() {
    return static_cast<std::size_t>(std::erase_if(rows_, [](const condition& c) {
        return lifetime_of(c.key.source) == condition_lifetime::verdict;
    }));
}

const condition* condition_table::current() const noexcept {
    const condition* best = nullptr;
    for (const auto& row : rows_) {
        if (best == nullptr || row.severity >= best->severity)
            best = &row;
    }
    return best;
}

std::string_view to_string(condition_source value) noexcept {
    switch (value) {
    case condition_source::startup: return "startup";
    case condition_source::instance: return "instance";
    case condition_source::account: return "account";
    case condition_source::folder: return "folder";
    case condition_source::label: return "label";
    case condition_source::labelling: return "labelling";
    case condition_source::shortcut: return "shortcut";
    case condition_source::session: return "session";
    case condition_source::credential: return "credential";
    case condition_source::store_lock: return "store_lock";
    case condition_source::window: return "window";
    case condition_source::window_wall: return "window_wall";
    case condition_source::read: return "read";
    case condition_source::probe: return "probe";
    }
    return "unknown";
}

std::string_view condition_table::message() const noexcept {
    const auto* shown = current();
    return shown == nullptr ? std::string_view{} : shown->message;
}

}
