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
#ifndef HEDGR_RUNTIME_REFUSAL_HPP
#define HEDGR_RUNTIME_REFUSAL_HPP

#include "hedgr.model/state/condition_table.hpp"
#include <string>

namespace hedgr::runtime {

/**
 * @brief Something the runtime refused, as the condition the manager raises.
 *
 * Returned rather than raised by the seeder and the services, because the
 * condition table and the clock it is stamped with belong to the manager.
 */
struct refusal final {
    model::state::condition_key key;
    std::string message;
};

}

#endif
