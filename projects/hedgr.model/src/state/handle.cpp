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
#include "hedgr.model/state/handle.hpp"

namespace hedgr::model::state {

std::uint32_t slab::allocate() {
    std::uint32_t index = 0;
    if (!free_slots.empty()) {
        index = free_slots.back();
        free_slots.pop_back();
    } else {
        index = static_cast<std::uint32_t>(alive.size());
        alive.push_back(0);
        generation.push_back(1);
    }
    alive[index] = 1;
    ++live_count;
    return index;
}

bool slab::release(std::uint32_t index) noexcept {
    if (!is_alive(index))
        return false;
    alive[index] = 0;
    ++generation[index];
    free_slots.push_back(index);
    --live_count;
    return true;
}

}
