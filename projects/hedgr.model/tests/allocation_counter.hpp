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
#ifndef HEDGR_MODEL_TESTS_ALLOCATION_COUNTER_HPP
#define HEDGR_MODEL_TESTS_ALLOCATION_COUNTER_HPP

#include <cstddef>

namespace hedgr::testing {

/**
 * @brief How many times this thread has called the global operator new.
 *
 * The test binary replaces operator new to count, so a test can state that a
 * frame-path call allocates nothing, which is a cost contract no timing can
 * pin as reliably.
 */
[[nodiscard]] std::size_t allocations() noexcept;

}

#endif
