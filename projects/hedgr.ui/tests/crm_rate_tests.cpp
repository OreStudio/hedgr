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
#include "hedgr.ui/render/crm_rate.hpp"
#include <catch2/catch_all.hpp>

using hedgr::ui::render::crm_decimal_places;
using hedgr::ui::render::crm_delta_is_zero;

TEST_CASE("a direct CRM rate uses its pair convention", "[crm]") {
    CHECK(crm_decimal_places(4, false, 0.72) == 4);
    CHECK(crm_decimal_places(2, false, 112.0) == 2);
    CHECK(crm_decimal_places(-1, false, 1.0) == 5);
}

TEST_CASE("a reciprocal CRM rate keeps the direct significant figures",
    "[crm]") {
    CHECK(crm_decimal_places(4, true, 0.72) == 5);
    CHECK(crm_decimal_places(2, true, 0.0089) == 7);
}

TEST_CASE("a CRM delta that rounds to zero is neutral", "[crm]") {
    CHECK(crm_delta_is_zero(0.0));
    CHECK(crm_delta_is_zero(0.0049));
    CHECK(crm_delta_is_zero(-0.0049));
    CHECK_FALSE(crm_delta_is_zero(0.005));
}
