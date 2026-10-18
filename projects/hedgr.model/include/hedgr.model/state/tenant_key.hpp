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
#ifndef HEDGR_MODEL_STATE_TENANT_KEY_HPP
#define HEDGR_MODEL_STATE_TENANT_KEY_HPP

#include <string>
#include <string_view>

namespace hedgr::model::state {

/**
 * @brief The key of something a tenant owns, from the tenant and its own name.
 *
 * Image identifiers, currency codes and image reads are all scoped by tenant.
 * One encoding keeps the cache, the runtime's in-flight set and any future
 * index from inventing their own separator and drifting apart.
 */
[[nodiscard]] inline std::string make_tenant_key(std::string_view tenant_id,
    std::string_view item) {
    std::string key;
    key.reserve(tenant_id.size() + item.size() + 1);
    key.append(tenant_id);
    key.push_back('\x1f');
    key.append(item);
    return key;
}

}

#endif
