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

#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/render/series_browser_view.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace render = hedgr::ui::render;

domain::market_series_row row(std::string id, std::string label,
    std::string asset_class = "FX", std::string tenor = "1Y",
    std::string party = "p1", std::string provider = "ore") {
    return domain::market_series_row{.id = std::move(id),
        .uri = "uri/" + label, .label = std::move(label),
        .asset_class = std::move(asset_class),
        .provider_code = std::move(provider), .party_id = std::move(party),
        .tenor = std::move(tenor)};
}

domain::market_instrument instrument(std::string id, std::string kind,
    std::string name, std::string currency = {}, std::string base = {},
    std::string quote = {}, std::vector<std::string> providers = {"ore"},
    std::int64_t age_seconds = 2) {
    domain::market_instrument out;
    out.instrument_id = std::move(id);
    out.kind = std::move(kind);
    out.name = std::move(name);
    out.currency = std::move(currency);
    out.base_currency = std::move(base);
    out.quote_currency = std::move(quote);
    out.instrument_type = "ir_swap";
    out.quote_type = "rate";
    out.party_id = "p1";
    for (auto& provider : providers) {
        out.sources.push_back(domain::instrument_source{
            .provider_code = std::move(provider),
            .provider_kind = "data_vendor",
            .series_id = "s-" + out.instrument_id,
            .as_of_ns = 1,
            .age_seconds = age_seconds,
            .stale = false});
    }
    return out;
}

render::series_browser_query query(std::uint64_t generation,
    std::string_view filter = {}, std::string_view kind = {},
    std::string_view provider = {}) {
    return {.generation = generation, .filter = filter, .kind = kind,
        .provider = provider};
}

std::vector<std::string> ids(const render::series_browser_view& view,
    const std::vector<domain::market_instrument>& instruments) {
    std::vector<std::string> result;
    for (const auto at : view.rows) {
        REQUIRE(at < instruments.size());
        result.push_back(instruments[at].instrument_id);
    }
    return result;
}

using strings = std::vector<std::string>;

}

TEST_CASE("the view lists the deployment's order and sorts nothing",
    "[browser][view]") {
    /*
     * The deployment answers by kind and then by name, and that is the order a
     * person reads. A second sort here could only disagree with it.
     */
    const std::vector instruments{instrument("c", "pair", "GBP/USD"),
        instrument("a", "curve", "AUD-AONIA"),
        instrument("b", "surface", "AUD/USD SSVI")};
    render::series_browser_view view;
    CHECK(render::update_series_browser_view(view, query(1), instruments));
    CHECK(ids(view, instruments) == strings{"c", "a", "b"});
}

TEST_CASE("a kind narrows the list and never changes what a row is",
    "[browser][view][kind]") {
    const std::vector instruments{instrument("p1", "pair", "AUD/USD"),
        instrument("c1", "curve", "AUD-AONIA"),
        instrument("c2", "curve", "AUD-SWAP"),
        instrument("s1", "surface", "AUD/USD SSVI")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1, "", "curve"),
        instruments);
    CHECK(ids(view, instruments) == strings{"c1", "c2"});
    /* The facet lists the kinds the rows hold, each once, in that order. */
    CHECK(view.kinds == strings{"curve"});

    render::update_series_browser_view(view, query(1), instruments);
    CHECK(view.rows.size() == 4);
    CHECK(view.kinds == strings{"pair", "curve", "surface"});
    /* A kind nothing holds is not offered. */
    render::update_series_browser_view(view, query(1, "", "ladder"),
        instruments);
    CHECK(view.rows.empty());
    CHECK(view.kinds.empty());
}

TEST_CASE("a hidden provider hides an instrument only when nothing else quotes it",
    "[browser][view][providers]") {
    const std::vector instruments{
        instrument("defn", "curve", "AUD-AONIA", "AUD", {}, {}, {"ore"}),
        instrument("both", "curve", "AUD-SWAP", "AUD", {}, {},
            {"ore", "bloomberg"})};
    const std::vector<std::string> hidden{"ore"};
    render::series_browser_view view;
    auto q = query(1);
    q.hidden_providers = hidden;
    CHECK(render::update_series_browser_view(view, q, instruments));
    /*
     * The curve only the reference provider quotes is hidden; the one a vendor
     * also quotes stays, because there is still something to price it with.
     */
    CHECK(ids(view, instruments) == strings{"both"});

    q.hidden_providers = {};
    CHECK(render::update_series_browser_view(view, q, instruments));
    CHECK(ids(view, instruments) == strings{"defn", "both"});
}

TEST_CASE("the view matches the name, the currencies, the types and the providers",
    "[browser][view][filter]") {
    const std::vector instruments{
        instrument("pair", "pair", "GBP/USD", "USD", "GBP", "USD",
            {"bloomberg"}),
        instrument("curve", "curve", "SONIA", "GBP", {}, {}, {"ore"}),
        instrument("surface", "surface", "EUR-USD SSVI", "EUR", "EUR", "USD",
            {"refdata"})};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1, "gbp"), instruments);
    CHECK(ids(view, instruments) == strings{"pair", "curve"});
    /* The quote side of a pair is a currency it names, so AUD finds GBP/AUD. */
    render::update_series_browser_view(view, query(1, "usd"), instruments);
    CHECK(ids(view, instruments) == strings{"pair", "surface"});
    render::update_series_browser_view(view, query(1, "bloom"), instruments);
    CHECK(ids(view, instruments) == strings{"pair"});
    render::update_series_browser_view(view, query(1, "ir_swap"), instruments);
    CHECK(ids(view, instruments).size() == 3);
    render::update_series_browser_view(view, query(1, "nzd"), instruments);
    CHECK(view.rows.empty());
}

TEST_CASE("the providers are listed once and sorted",
    "[browser][view][provider]") {
    const std::vector instruments{
        instrument("a", "pair", "A", "USD", "EUR", "USD", {"ore"}),
        instrument("b", "pair", "B", "USD", "GBP", "USD", {"bloomberg"}),
        instrument("c", "pair", "C", "USD", "GBP", "USD",
            {"bloomberg", "ore"}),
        instrument("d", "pair", "D", "USD", "GBP", "USD", {})};
    render::series_browser_view view;
    CHECK(render::update_series_browser_view(view, query(1), instruments));
    CHECK(view.providers == strings{"bloomberg", "ore"});
}

TEST_CASE("the view is kept while the query is unchanged", "[browser][view]") {
    const std::vector instruments{instrument("a", "pair", "A"),
        instrument("b", "pair", "B")};
    render::series_browser_view view;
    CHECK(render::update_series_browser_view(view, query(7, "a", "pair"),
        instruments));
    CHECK_FALSE(render::update_series_browser_view(view, query(7, "a", "pair"),
        instruments));
    CHECK(view.builds == 1);
}

TEST_CASE("each part of the query rebuilds the view on its own",
    "[browser][view]") {
    const std::vector instruments{
        instrument("a", "pair", "EUR/USD", "USD", "EUR", "USD", {"ore"}),
        instrument("b", "curve", "ESTR", "EUR", {}, {}, {"bloomberg"}),
        instrument("c", "curve", "SOFR", "USD", {}, {}, {"ore"})};
    const auto from_base = [&](const render::series_browser_query& changed,
                               const strings& expected) {
        render::series_browser_view view;
        render::update_series_browser_view(view, query(1), instruments);
        REQUIRE(view.rows.size() == 3);
        CHECK(render::update_series_browser_view(view, changed, instruments));
        CHECK(ids(view, instruments) == expected);
    };
    from_base(query(1, "sofr"), {"c"});
    from_base(query(1, "", "curve"), {"b", "c"});
    from_base(query(1, "", "", "bloomberg"), {"b"});
    from_base(query(2), {"a", "b", "c"});
}

TEST_CASE("a list replaced by a shorter one is never read past its end",
    "[browser][view]") {
    std::vector<domain::market_instrument> instruments;
    for (int i = 0; i < 50; ++i)
        instruments.push_back(instrument("i-" + std::to_string(i), "pair",
            "L" + std::to_string(i)));
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1), instruments);
    REQUIRE(view.rows.size() == 50);

    instruments = {instrument("only", "pair", "Only")};
    CHECK(render::update_series_browser_view(view, query(2), instruments));
    CHECK(ids(view, instruments) == strings{"only"});

    instruments.clear();
    CHECK(render::update_series_browser_view(view, query(0), instruments));
    CHECK(view.rows.empty());
}

TEST_CASE("a currency finds both sides of a pair and a rate",
    "[browser][view][currency]") {
    /*
     * A desk searches by currency: "AUD" is every curve quoted in AUD and every
     * pair quoted in AUD, whichever side it is on. The currencies are the
     * instrument's own fields, so a pair is found from its quote as well.
     */
    const std::vector instruments{
        instrument("ir-aud", "curve", "AUD-AONIA", "AUD"),
        instrument("fx-aud", "pair", "AUD/USD", "USD", "AUD", "USD"),
        instrument("fx-gbp-aud", "pair", "GBP/AUD", "AUD", "GBP", "AUD"),
        instrument("fx-eur", "pair", "EUR/JPY", "JPY", "EUR", "JPY")};
    render::series_browser_view view;

    render::update_series_browser_view(view, query(1, "aud"), instruments);
    CHECK(ids(view, instruments) == strings{"ir-aud", "fx-aud", "fx-gbp-aud"});

    render::update_series_browser_view(view,
        query(1, "aud", "curve"), instruments);
    CHECK(ids(view, instruments) == strings{"ir-aud"});

    render::update_series_browser_view(view, query(1, "nzd"), instruments);
    CHECK(ids(view, instruments).empty());
}

TEST_CASE("the party list parses the session's column once", "[browser][parties]") {
    const std::string serialised = "p1\x1f" "First\x1ep2\x1f" "Second";
    render::party_list list;
    CHECK(render::update_party_list(list, serialised, "p1", "First"));
    REQUIRE(list.size() == 2);
    CHECK(std::string{list.id(0)} == "p1");
    CHECK(std::string{list.name(0)} == "First");
    CHECK(std::string{list.id(1)} == "p2");
    CHECK(std::string{list.name(1)} == "Second");
    REQUIRE(list.names.size() == 2);
    CHECK(std::string{list.names[0]} == "First");
    CHECK(std::string{list.names[1]} == "Second");
    CHECK(list.index_of("p1") == 0);
    CHECK(list.index_of("p2") == 1);
    CHECK(list.index_of("p9") == list.size());
    CHECK_FALSE(render::update_party_list(list, serialised, "p1", "First"));
    CHECK(list.builds == 1);

    CHECK(render::update_party_list(list, "p3\x1f" "Third", "p1", "First"));
    REQUIRE(list.size() == 1);
    CHECK(std::string{list.name(0)} == "Third");
}

TEST_CASE("a login without a party list still lists its own party",
    "[browser][parties]") {
    render::party_list list;
    render::update_party_list(list, "", "p9", "Nine");
    REQUIRE(list.size() == 1);
    CHECK(std::string{list.id(0)} == "p9");
    CHECK(std::string{list.name(0)} == "Nine");
    CHECK(render::update_party_list(list, "", "p8", "Eight"));
    CHECK(std::string{list.id(0)} == "p8");
    render::update_party_list(list, "", "", "");
    CHECK(list.empty());
}

namespace {

/** @brief A swap rung: one series per forward start, all at the same tenor. */
domain::market_series_row rung(std::string id, std::string forward_start) {
    domain::market_series_row row{.id = std::move(id),
        .uri = "oresmd://ir/AUD?fwd_start=" + forward_start,
        .label = "IR AUD 1D",
        .asset_class = "IR",
        .provider_code = "demo",
        .party_id = "p1",
        .tenor = "1D"};
    row.asset = domain::market_asset_of("IR");
    row.identity.asset = "ir";
    row.identity.scope = "AUD";
    row.identity.instrument_type = "ir_swap";
    row.identity.quote_type = "rate";
    row.identity.party_id = "p1";
    row.identity.provider_code = "demo";
    row.identity.fields = {{.name = "fwd_start", .text = forward_start},
        {.name = "term", .text = "1D"}};
    return row;
}

}

/*
 * A swap ladder is series that differ by their forward start, not by their
 * tenor: every rung carries the same `1D` index tenor, so a curve gathered by
 * "the URI without its tenor" answers with one rung and the plot shows one
 * point. The grouping must follow the field the deployment actually varies.
 */
TEST_CASE("a curve is grouped by the field its rungs vary on", "[curve]") {
    const std::vector<domain::market_series_row> catalogue{
        rung("s-0d", "0D"), rung("s-3m", "3M"), rung("s-1y", "1Y")};

    const auto group = render::curve_of(catalogue, catalogue.front());
    CHECK(group.field == "fwd_start");
    REQUIRE(group.members.size() == 3);
    CHECK(group.rungs == std::vector<std::string>{"0D", "3M", "1Y"});
    CHECK(group.members[0]->id == "s-0d");
    CHECK(group.members[2]->id == "s-1y");
}

TEST_CASE("a curve whose rung is its tenor is grouped by that", "[curve]") {
    std::vector<domain::market_series_row> catalogue;
    for (const auto& tenor : {"1Y", "10Y", "2Y"}) {
        auto held = row(std::string{"s-"} + tenor, "IR GBP", "IR", tenor);
        held.identity.asset = "ir";
        held.identity.scope = "GBP";
        held.identity.instrument_type = "ir_swap";
        held.identity.quote_type = "rate";
        held.identity.party_id = "p1";
        held.identity.provider_code = "ore";
        held.identity.fields = {{.name = "tenor", .text = tenor}};
        catalogue.push_back(std::move(held));
    }

    const auto group = render::curve_of(catalogue, catalogue.front());
    CHECK(group.field == "tenor");
    CHECK(group.rungs == std::vector<std::string>{"1Y", "2Y", "10Y"});
}

TEST_CASE("a curve of one series is one rung, honestly", "[curve]") {
    const std::vector<domain::market_series_row> catalogue{rung("s-0d", "0D")};
    const auto group = render::curve_of(catalogue, catalogue.front());
    REQUIRE(group.members.size() == 1);
    /* Nothing varies, so the rung is the series' own tenor, not a guess. */
    CHECK(group.field.empty());
    CHECK(group.rungs == std::vector<std::string>{"1D"});
}
