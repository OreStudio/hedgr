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
#include "allocation_counter.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tenant_image_map.hpp"
#include "hedgr.model/state/term_structure_table.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <string>
#include <vector>

/*
 * The frame loop looks every market window's rows up once per frame by
 * identifiers too long for the small-string buffer. These tests pin that a
 * lookup of a held row borrows those identifiers instead of copying them.
 */

namespace {

using hedgr::testing::allocations;

const std::string instance = "6f1c2a3b-4d5e-4f60-8a71-92b3c4d5e6f7";
const std::string account = "0b6f3c1e-8a2d-4f4b-9c1e-5d7a2b3c4d5e";
const std::string series = "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d";
const std::string party = "11112222-3333-4444-5555-666677778888";
const std::string tenant = "99990000-aaaa-4bbb-8ccc-ddddeeeeffff";
const std::string image = "12341234-5678-4abc-9def-0123456789ab";
const std::string curve = "IR GBP IR_SWAP ESTR overnight index curve";

constexpr int lookups = 1000;

}

TEST_CASE("looking up a held chart allocates nothing", "[cost][market]") {
    hedgr::model::state::series_table market;
    market.apply_catalogue(instance, account, {});
    const auto held = market.intern(instance, account, series);
    const std::vector<hedgr::model::domain::market_point> observed{
        {.at_ns = 1'000'000'000, .value = 1.1}};
    market.seed(held, observed, 1);

    const auto before = allocations();
    std::size_t found = 0;
    for (int i = 0; i < lookups; ++i) {
        found += market.find(instance, account, series).has_value();
        found += market.catalogue(instance, account) != nullptr;
        found += market.columns(held, hedgr::model::domain::market_level::fine)
                     .size;
    }
    CHECK(allocations() - before == 0);
    CHECK(found == 3 * lookups);
    CHECK_FALSE(market.find(instance, account, party).has_value());
    CHECK(market.catalogue(instance, party) == nullptr);
}

TEST_CASE("looking up a held CRM batch allocates nothing", "[cost][crm]") {
    hedgr::model::state::crm_table crm;
    crm.set(instance, account, party, "majors", false, {}, {}, 1, 0);
    crm.set(instance, account, party, "majors", true, {}, {}, 2, 0);
    crm.set_names(instance, account, party, {"majors"});

    const auto before = allocations();
    std::size_t found = 0;
    for (int i = 0; i < lookups; ++i) {
        found += crm.get(instance, account, party, "majors", false) != nullptr;
        found += crm.names(instance, account, party) != nullptr;
    }
    CHECK(allocations() - before == 0);
    CHECK(found == 2 * lookups);
    CHECK(crm.get(instance, account, party, "majors", false)->observed_as_of_ns
        == 1);
    CHECK(crm.get(instance, account, party, "majors", true)->observed_as_of_ns
        == 2);
}

TEST_CASE("looking up a held curve allocates nothing", "[cost][rates]") {
    hedgr::model::state::term_structure_table curves;
    curves.set(instance, account, curve, "IR GBP", {});

    const auto before = allocations();
    std::size_t found = 0;
    for (int i = 0; i < lookups; ++i)
        found += curves.get(instance, account, curve) != nullptr;
    CHECK(allocations() - before == 0);
    CHECK(found == static_cast<std::size_t>(lookups));
}

TEST_CASE("looking up a held flag allocates nothing", "[cost][images]") {
    hedgr::model::state::tenant_image_map map;
    map.set(tenant, {{"GBP", image}});
    hedgr::model::state::image_cache cache;
    REQUIRE(cache.put(tenant, image,
        {.mime_type = "image/png", .data = std::vector<std::uint8_t>(4, 1)}));

    const auto before = allocations();
    std::size_t found = 0;
    for (int i = 0; i < lookups; ++i) {
        const auto* id = map.image_for(tenant, "GBP");
        found += id != nullptr && cache.contains(tenant, *id)
            && cache.get(tenant, *id) != nullptr;
    }
    CHECK(allocations() - before == 0);
    CHECK(found == static_cast<std::size_t>(lookups));
    CHECK(cache.get(image, tenant) == nullptr);
}

TEST_CASE("the cost of one chart lookup", "[!benchmark][market]") {
    constexpr std::size_t charts = 200;
    hedgr::model::state::series_table market;
    const std::vector<hedgr::model::domain::market_point> observed{
        {.at_ns = 1'000'000'000, .value = 1.1}};
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < charts; ++i) {
        ids.push_back(series + std::to_string(i));
        market.seed(market.intern(instance, account, ids.back()), observed, 1);
    }
    std::size_t next = 0;
    BENCHMARK("series_table::find over 200 charts") {
        next = (next + 1) % charts;
        return market.find(instance, account, ids[next]);
    };
}
