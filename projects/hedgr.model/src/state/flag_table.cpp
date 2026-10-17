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
#include "hedgr.model/state/flag_table.hpp"
#include <utility>

namespace hedgr::model::state {

namespace {

constexpr char separator = '\x1f';

[[nodiscard]] std::string key_of(std::string_view instance_id,
    std::string_view account_id, std::string_view code) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + code.size() + 2);
    key.append(instance_id);
    key.push_back(separator);
    key.append(account_id);
    key.push_back(separator);
    key.append(code);
    return key;
}

}

void flag_table::begin(std::string_view instance_id,
    std::string_view account_id, std::string_view code) {
    auto& entry = flags_[key_of(instance_id, account_id, code)];
    entry.state = flag_state::reading;
    entry.error.clear();
}

void flag_table::set(std::string_view instance_id,
    std::string_view account_id, std::string_view code,
    std::string mime_type, std::vector<std::uint8_t> data) {
    auto& entry = flags_[key_of(instance_id, account_id, code)];
    entry.state = flag_state::ready;
    entry.mime_type = std::move(mime_type);
    entry.data = std::move(data);
    entry.error.clear();
}

void flag_table::fail(std::string_view instance_id,
    std::string_view account_id, std::string_view code,
    std::string message) {
    auto& entry = flags_[key_of(instance_id, account_id, code)];
    entry.state = flag_state::failed;
    entry.error = std::move(message);
}

const flag_image* flag_table::get(std::string_view instance_id,
    std::string_view account_id, std::string_view code) const {
    const auto found =
        flags_.find(key_of(instance_id, account_id, code));
    return found == flags_.end() ? nullptr : &found->second;
}

std::vector<std::pair<std::string, flag_image>> flag_table::ready(
    std::string_view instance_id, std::string_view account_id) const {
    std::vector<std::pair<std::string, flag_image>> result;
    const auto prefix = std::string{instance_id} + separator
        + std::string{account_id} + separator;
    for (const auto& [key, image] : flags_) {
        if (image.state != flag_state::ready)
            continue;
        if (key.rfind(prefix, 0) != 0)
            continue;
        result.emplace_back(key.substr(prefix.size()), image);
    }
    return result;
}

std::vector<std::pair<std::string, const flag_image*>>
flag_table::all_ready() const {
    std::vector<std::pair<std::string, const flag_image*>> result;
    for (const auto& [key, image] : flags_) {
        if (image.state != flag_state::ready)
            continue;
        const auto separator_at = key.rfind(separator);
        if (separator_at == std::string::npos)
            continue;
        result.emplace_back(key.substr(separator_at + 1), &image);
    }
    return result;
}

void flag_table::clear() {
    flags_.clear();
}

}
