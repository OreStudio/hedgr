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
#include "hedgr.store/sqlite/error.hpp"

namespace hedgr::store::sqlite {

std::string_view to_string(store_errc value) noexcept {
    switch (value) {
    case store_errc::open_failed:
        return "open_failed";
    case store_errc::prepare_failed:
        return "prepare_failed";
    case store_errc::bind_failed:
        return "bind_failed";
    case store_errc::step_failed:
        return "step_failed";
    case store_errc::constraint_violated:
        return "constraint_violated";
    case store_errc::not_found:
        return "not_found";
    case store_errc::corrupt_row:
        return "corrupt_row";
    case store_errc::closed:
        return "closed";
    }
    return "unknown";
}

}
