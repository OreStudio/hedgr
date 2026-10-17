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
#ifndef HEDGR_MODEL_STATE_FLAG_TABLE_HPP
#define HEDGR_MODEL_STATE_FLAG_TABLE_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hedgr::model::state {

/** @brief Where one tenant image read stands. */
enum class flag_state : std::uint8_t {
    unread = 0,
    reading = 1,
    ready = 2,
    failed = 3,
};

/** @brief One currency flag as the assets service sent it. */
struct flag_image final {
    flag_state state = flag_state::unread;
    std::string mime_type;
    std::vector<std::uint8_t> data;
    std::string error;
};

/**
 * @brief The flags each session has read, keyed by session and currency code.
 *
 * A flag is an assets image named by the currency's lower-case ISO code. The
 * table holds the bytes rather than a texture: uploading belongs to the frame
 * loop, which owns the graphics context, and this table must be testable
 * without one.
 */
class HEDGR_MODEL_EXPORT flag_table final {
public:
    void begin(std::string_view instance_id, std::string_view account_id,
        std::string_view code);
    void set(std::string_view instance_id, std::string_view account_id,
        std::string_view code, std::string mime_type,
        std::vector<std::uint8_t> data);
    void fail(std::string_view instance_id, std::string_view account_id,
        std::string_view code, std::string message);

    [[nodiscard]] const flag_image* get(std::string_view instance_id,
        std::string_view account_id, std::string_view code) const;

    /** @brief Every ready flag of one session, for the texture cache. */
    [[nodiscard]] std::vector<std::pair<std::string, flag_image>>
    ready(std::string_view instance_id, std::string_view account_id) const;

    /** @brief Every ready flag, by currency code, for a shared texture cache. */
    [[nodiscard]] std::vector<std::pair<std::string, const flag_image*>>
    all_ready() const;

    void clear();

private:
    std::unordered_map<std::string, flag_image> flags_;
};

}

#endif
