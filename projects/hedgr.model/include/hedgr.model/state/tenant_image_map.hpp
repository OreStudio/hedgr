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
#ifndef HEDGR_MODEL_STATE_TENANT_IMAGE_MAP_HPP
#define HEDGR_MODEL_STATE_TENANT_IMAGE_MAP_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hedgr::model::state {

/** @brief Where a tenant's currency-to-image map stands. */
enum class image_map_state : std::uint8_t {
    unread = 0,
    reading = 1,
    ready = 2,
    failed = 3,
};

/** @brief One tenant's currency codes and the image each names, by key. */
struct tenant_images final {
    image_map_state state = image_map_state::unread;
    std::unordered_map<std::string, std::string> currencies;
    std::string error;
};

/**
 * @brief Which image each currency of a tenant uses, as refdata names it.
 *
 * The map is the one place a screen learns a flag's identity; the bytes live in
 * the image cache under that identity. It is read once per tenant, is small,
 * and an image identifier never changes, so there is no expiry.
 */
class HEDGR_MODEL_EXPORT tenant_image_map final {
public:
    void begin(std::string_view tenant_id);
    void set(std::string_view tenant_id,
        std::vector<std::pair<std::string, std::string>> currencies);
    void fail(std::string_view tenant_id, std::string message);

    [[nodiscard]] const tenant_images* get(std::string_view tenant_id) const;

    /** @brief The image identifier for a currency, or nullptr. */
    [[nodiscard]] const std::string* image_for(std::string_view tenant_id,
        std::string_view currency) const;

    void clear();

private:
    std::unordered_map<std::string, tenant_images> tenants_;
};

}

#endif
