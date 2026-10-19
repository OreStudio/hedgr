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
#include "hedgr.model/state/tenant_image_map.hpp"
#include <utility>

namespace hedgr::model::state {

void tenant_image_map::begin(std::string_view tenant_id) {
    auto& entry = slot_in(tenants_, {tenant_id});
    entry.state = image_map_state::reading;
    entry.currencies.clear();
    entry.error.clear();
}

void tenant_image_map::set(std::string_view tenant_id,
    std::vector<std::pair<std::string, std::string>> currencies) {
    auto& entry = slot_in(tenants_, {tenant_id});
    entry.state = image_map_state::ready;
    entry.currencies.clear();
    for (auto& [currency, image_id] : currencies)
        entry.currencies.insert_or_assign(std::move(currency),
            std::move(image_id));
    entry.error.clear();
}

void tenant_image_map::fail(std::string_view tenant_id, std::string message) {
    auto& entry = slot_in(tenants_, {tenant_id});
    entry.state = image_map_state::failed;
    entry.error = std::move(message);
}

const tenant_images* tenant_image_map::get(std::string_view tenant_id) const {
    return find_in(tenants_, {tenant_id});
}

const std::string* tenant_image_map::image_for(std::string_view tenant_id,
    std::string_view currency) const {
    const auto* images = get(tenant_id);
    if (images == nullptr)
        return nullptr;
    const auto found = images->currencies.find(std::string{currency});
    return found == images->currencies.end() ? nullptr : &found->second;
}

void tenant_image_map::clear() {
    tenants_.clear();
}

}
