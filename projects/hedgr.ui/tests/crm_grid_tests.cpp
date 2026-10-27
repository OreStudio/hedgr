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
#include "hedgr.ui/render/crm_grid.hpp"
#include <catch2/catch_all.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
using hedgr::ui::render::build_crm_grid;
using hedgr::ui::render::crm_delta_is_zero;
using hedgr::ui::render::crm_grid;
using hedgr::ui::render::crm_pair_cards;
using hedgr::ui::render::crm_series_for_pair;

domain::crm_cell cell(std::string base, std::string quote, double rate,
    std::string status = "fresh") {
    return domain::crm_cell{.crm_name = "majors",
        .base_currency_code = std::move(base),
        .quote_currency_code = std::move(quote), .rate = rate,
        .status = std::move(status)};
}

crm_grid grid_of(std::vector<domain::crm_cell> cells,
    std::vector<domain::crm_convention> conventions = {}) {
    hedgr::model::state::crm_snapshot batch;
    batch.cells = std::move(cells);
    batch.conventions = std::move(conventions);
    batch.version = 7;
    std::set<std::string> seen;
    return build_crm_grid(batch, "majors", seen);
}

std::string rate_at(const crm_grid& grid, std::string_view pair) {
    for (const auto& entry : grid.cells)
        if (entry.pair == pair)
            return entry.rate;
    return "missing";
}

domain::market_series_row fx_series(std::string id, std::string base,
    std::string quote, std::string party = "p1",
    std::string provider = "bloomberg",
    std::string instrument = "fx_spot") {
    domain::market_series_row row;
    row.id = std::move(id);
    row.label = "FX " + base + "/" + quote;
    row.asset_class = "FX";
    row.party_id = std::move(party);
    row.provider_code = std::move(provider);
    row.uri = "oresmd:///fx/" + base + "?instrument=" + instrument + "&ccy="
        + quote;
    /* A parsed identity, as the catalogue read leaves it. */
    row.identity.asset = "fx";
    row.identity.scope = base;
    row.identity.instrument_type = std::move(instrument);
    row.identity.quote_type = "rate";
    row.identity.party_id = row.party_id;
    row.identity.provider_code = row.provider_code;
    row.identity.fields.push_back({.name = "ccy", .text = quote});
    return row;
}

const domain::crm_convention usd_jpy{.pair_code = "USD/JPY",
    .decimal_places = 3, .pip_factor = 0.01, .tick_size = 0.1};
const domain::crm_convention eur_usd_half_pip{.pair_code = "EUR/USD",
    .decimal_places = 5, .pip_factor = 0.0001, .tick_size = 0.5};

}

TEST_CASE("a reciprocal CRM cell takes its convention from the reverse pair",
    "[crm]") {
    auto reciprocal = cell("JPY", "USD", 0.00666134);
    reciprocal.reciprocal = true;
    const auto grid = grid_of({cell("USD", "JPY", 150.1234), reciprocal},
        {usd_jpy});
    CHECK(rate_at(grid, "USD/JPY") == "150.123");
    CHECK(rate_at(grid, "JPY/USD") == "0.00666134");
}

TEST_CASE("a direct CRM rate snaps to its pair's tick", "[crm]") {
    const auto grid = grid_of({cell("EUR", "USD", 1.10287)},
        {eur_usd_half_pip});
    CHECK(rate_at(grid, "EUR/USD") == "1.10285");
}

TEST_CASE("a CRM rate with no convention shows five places", "[crm]") {
    const auto grid = grid_of({cell("GBP", "USD", 1.2)});
    CHECK(rate_at(grid, "GBP/USD") == "1.20000");
    CHECK(rate_at(grid, "USD/GBP").empty());
}

TEST_CASE("a CRM headline counts the cells the grid draws", "[crm]") {
    const auto grid = grid_of({cell("EUR", "USD", 0.0, "unavailable"),
        cell("EUR", "USD", 1.1), cell("GBP", "USD", 0.0, "disconnected")});
    CHECK(grid.version == 7);
    CHECK(grid.currencies == std::vector<std::string>{"EUR", "GBP", "USD"});
    CHECK(grid.counts.fresh == 1);
    CHECK(grid.counts.unavailable == 0);
    CHECK(grid.counts.disconnected == 1);
    CHECK(rate_at(grid, "EUR/USD") == "1.10000");
}

TEST_CASE("a CRM delta that rounds to zero is neutral", "[crm]") {
    CHECK(crm_delta_is_zero(0.0));
    CHECK(crm_delta_is_zero(0.0049));
    CHECK(crm_delta_is_zero(-0.0049));
    CHECK_FALSE(crm_delta_is_zero(0.005));
}

TEST_CASE("a CRM pair finds the FX series of its own party", "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("a", "EUR", "USD", "other"),
        fx_series("b", "EUR", "USD")};
    const auto* row = crm_series_for_pair(series, "p1", "EUR", "USD");
    REQUIRE(row != nullptr);
    CHECK(row->id == "b");
    CHECK(crm_series_for_pair(series, "p1", "USD", "EUR") == nullptr);
}

TEST_CASE("a CRM pair prefers a data vendor over an earlier reference series",
    "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("ore-row", "EUR", "USD", "p1", "ore"),
        fx_series("vendor-row", "EUR", "USD", "p1", "bloomberg")};
    const std::vector<std::string> reference{"ore"};

    const auto* preferred
        = crm_series_for_pair(series, "p1", "EUR", "USD", reference);
    REQUIRE(preferred != nullptr);
    CHECK(preferred->id == "vendor-row");

    /* With no kinds known, the catalogue's own order still decides. */
    const auto* first = crm_series_for_pair(series, "p1", "EUR", "USD");
    REQUIRE(first != nullptr);
    CHECK(first->id == "ore-row");
}

TEST_CASE("a CRM pair takes the spot, not a forward or an option",
    "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("fwd", "EUR", "USD", "p1", "bloomberg", "fx_fwd"),
        fx_series("opt", "EUR", "USD", "p1", "bloomberg", "fx_option"),
        fx_series("spot", "EUR", "USD", "p1", "bloomberg", "fx_spot")};

    const auto* chosen = crm_series_for_pair(series, "p1", "EUR", "USD");
    REQUIRE(chosen != nullptr);
    CHECK(chosen->id == "spot");

    /* A pair held only by a forward has no CRM series at all. */
    const std::vector<domain::market_series_row> forward_only{
        fx_series("fwd", "EUR", "USD", "p1", "bloomberg", "fx_fwd")};
    CHECK(crm_series_for_pair(forward_only, "p1", "EUR", "USD") == nullptr);
}

TEST_CASE("a row without a parsed identity still matches by its URI",
    "[crm]") {
    auto legacy = fx_series("legacy", "EUR", "USD");
    legacy.identity = {};
    const std::vector<domain::market_series_row> series{legacy};
    const auto* row = crm_series_for_pair(series, "p1", "EUR", "USD");
    REQUIRE(row != nullptr);
    CHECK(row->id == "legacy");
}

TEST_CASE("CRM mini plots take the data vendor's series for a pair", "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("ore-row", "EUR", "USD", "p1", "ore"),
        fx_series("vendor-row", "EUR", "USD", "p1", "bloomberg")};
    const std::vector<std::string> reference{"ore"};
    std::size_t unmatched = 0;
    const auto cards = crm_pair_cards(series, "p1", {"EUR", "USD"}, unmatched,
        reference);
    REQUIRE(cards.size() == 1);
    CHECK(cards[0].id == "vendor-row");
}

TEST_CASE("CRM mini plots open one card per pair, skipping pairs with none",
    "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("eurusd", "EUR", "USD"),
        fx_series("usdeur", "USD", "EUR"),
        fx_series("gbpusd", "GBP", "USD")};
    std::size_t unmatched = 99;
    const auto cards = crm_pair_cards(series, "p1",
        {"EUR", "GBP", "USD"}, unmatched);
    REQUIRE(cards.size() == 3);
    CHECK(cards[0].id == "eurusd");
    CHECK(cards[1].id == "usdeur");
    CHECK(cards[2].id == "gbpusd");
    CHECK(unmatched == 1);
}

TEST_CASE("CRM mini plots count a pair once however many series it has",
    "[crm]") {
    const std::vector<domain::market_series_row> series{
        fx_series("a", "EUR", "USD"), fx_series("a", "EUR", "USD")};
    std::size_t unmatched = 0;
    CHECK(crm_pair_cards(series, "p1", {"EUR", "USD"}, unmatched).size() == 1);
    CHECK(unmatched == 0);
}
