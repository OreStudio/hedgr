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

render::series_browser_query query(std::uint64_t generation,
    std::string_view filter = {}, std::string_view asset_class = {},
    std::string_view party = {}, std::string_view provider = {}) {
    return {.generation = generation, .filter = filter,
        .asset_class = asset_class, .party = party, .provider = provider};
}

std::vector<std::string> ids(const render::series_browser_view& view,
    const std::vector<domain::market_series_row>& catalogue) {
    std::vector<std::string> result;
    for (const auto at : view.rows) {
        REQUIRE(at < catalogue.size());
        result.push_back(catalogue[at].id);
    }
    return result;
}

using strings = std::vector<std::string>;

}

TEST_CASE("the series view orders by tenor and then label", "[browser][view]") {
    const std::vector catalogue{row("a", "B", "FX", "1Y"),
        row("b", "A", "FX", "1Y"), row("c", "Z", "FX", "1M"),
        row("d", "Y", "FX", "2W")};
    render::series_browser_view view;
    CHECK(render::update_series_browser_view(view, query(1), catalogue));
    CHECK(ids(view, catalogue) == strings{"d", "c", "b", "a"});
}

TEST_CASE("the series view drops a hidden provider and shows it again",
    "[browser][view][providers]") {
    const std::vector catalogue{
        row("ore-row", "EUR/USD", "FX", "1Y", "p1", "ore"),
        row("vendor-row", "GBP/USD", "FX", "1Y", "p1", "bloomberg")};
    render::series_browser_view view;
    auto q = query(1);
    const std::vector<std::string> hidden{"ore"};
    q.hidden_providers = hidden;
    CHECK(render::update_series_browser_view(view, q, catalogue));
    CHECK(ids(view, catalogue) == strings{"vendor-row"});

    q.hidden_providers = {};
    CHECK(render::update_series_browser_view(view, q, catalogue));
    CHECK(ids(view, catalogue) == strings{"ore-row", "vendor-row"});
}

TEST_CASE("a parsed asset decides the curve view, not the display class",
    "[browser][view]") {
    auto rates = row("c", "C", "RATES", "1Y");
    rates.asset = domain::market_asset::ir;
    const std::vector catalogue{rates};
    render::series_browser_view view;
    /* The display class says RATES; the parsed asset is what chooses curves. */
    CHECK(render::update_series_browser_view(
        view, query(1, "", "IR"), catalogue));
    CHECK(ids(view, catalogue) == strings{"c"});
}

TEST_CASE("the series view matches the filter on label, class, uri and provider",
    "[browser][view]") {
    const std::vector catalogue{row("a", "GBP/USD", "FX"),
        row("b", "EUR/USD", "RATES", "1Y", "p1", "bloomberg"),
        row("c", "SONIA", "IR")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1, "gbp"), catalogue);
    CHECK(ids(view, catalogue) == strings{"a"});
    render::update_series_browser_view(view, query(1, "rates"), catalogue);
    CHECK(ids(view, catalogue) == strings{"b"});
    render::update_series_browser_view(view, query(1, "uri/son"), catalogue);
    CHECK(ids(view, catalogue) == strings{"c"});
    render::update_series_browser_view(view, query(1, "bloom"), catalogue);
    CHECK(ids(view, catalogue) == strings{"b"});
}

TEST_CASE("two providers of one item are two rows of the series view",
    "[browser][view]") {
    const std::vector catalogue{row("a", "GBP/USD", "FX", "1Y", "p1", "ore"),
        row("b", "GBP/USD", "FX", "1Y", "p1", "bloomberg")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1, "gbp"), catalogue);
    CHECK(ids(view, catalogue) == strings{"a", "b"});
    render::update_series_browser_view(view, query(1, "gbp", "FX", "p1"),
        catalogue);
    CHECK(ids(view, catalogue) == strings{"a", "b"});
    render::update_series_browser_view(view, query(1, "bloomberg"), catalogue);
    CHECK(ids(view, catalogue) == strings{"b"});
}

TEST_CASE("the series view is kept while the query is unchanged",
    "[browser][view]") {
    const std::vector catalogue{row("a", "A"), row("b", "B")};
    render::series_browser_view view;
    CHECK(render::update_series_browser_view(view, query(7, "a", "FX", "p1"),
        catalogue));
    CHECK_FALSE(render::update_series_browser_view(view,
        query(7, "a", "FX", "p1"), catalogue));
    CHECK(view.builds == 1);
}

TEST_CASE("each part of the query rebuilds the series view on its own",
    "[browser][view]") {
    const std::vector catalogue{row("a", "A", "FX", "1Y", "p1"),
        row("b", "B", "FX", "1Y", "p2", "bloomberg"),
        row("c", "C", "RATES", "1Y", "p1")};
    const auto from_base = [&](const render::series_browser_query& changed,
                               const strings& expected) {
        render::series_browser_view view;
        render::update_series_browser_view(view, query(1), catalogue);
        REQUIRE(view.rows.size() == 3);
        CHECK(render::update_series_browser_view(view, changed, catalogue));
        CHECK(ids(view, catalogue) == expected);
    };
    from_base(query(1, "b"), {"b"});
    from_base(query(1, "", "RATES"), {"c"});
    from_base(query(1, "", "", "p2"), {"b"});
    from_base(query(2), {"a", "b", "c"});
}

TEST_CASE("a catalogue replaced by a shorter one is never read past its end",
    "[browser][view]") {
    std::vector<domain::market_series_row> catalogue;
    for (int i = 0; i < 50; ++i)
        catalogue.push_back(row("s-" + std::to_string(i), "L" + std::to_string(i)));
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1), catalogue);
    REQUIRE(view.rows.size() == 50);

    catalogue = {row("only", "Only")};
    CHECK(render::update_series_browser_view(view, query(2), catalogue));
    CHECK(ids(view, catalogue) == strings{"only"});

    catalogue.clear();
    CHECK(render::update_series_browser_view(view, query(0), catalogue));
    CHECK(view.rows.empty());
}

TEST_CASE("a refresh with the same rows in a new order shows the new order",
    "[browser][view]") {
    std::vector catalogue{row("a", "Same"), row("b", "Same"), row("c", "Same")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1), catalogue);
    CHECK(ids(view, catalogue) == strings{"a", "b", "c"});

    catalogue = {row("c", "Same"), row("a", "Same"), row("b", "Same")};
    CHECK(render::update_series_browser_view(view, query(2), catalogue));
    CHECK(ids(view, catalogue) == strings{"c", "a", "b"});
}

TEST_CASE("the IR view lists one curve per IR series, ordered by tenor and curve label",
    "[browser][view]") {
    auto sonia = row("a", "SONIA 1D", "IR", "1D");
    sonia.curve_label = "SONIA curve";
    const std::vector catalogue{row("x", "AAA", "FX"),
        row("b", "ESTR 1D", "IR", "1D"), sonia,
        row("c", "SOFR 1M", "IR", "1M"),
        row("d", "OTHER", "IR", "1D", "p2")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1, "", "IR", "p1"), catalogue);
    CHECK(ids(view, catalogue) == strings{"b", "a", "c"});
    render::update_series_browser_view(view, query(1, "sofr", "IR", "p1"),
        catalogue);
    CHECK(ids(view, catalogue) == strings{"c"});
    CHECK(render::curve_label_of(catalogue[2]) == "SONIA curve");
    CHECK(render::curve_label_of(catalogue[1]) == "ESTR 1D");
}

TEST_CASE("the asset classes are listed once, in order, and kept per generation",
    "[browser][view]") {
    const std::vector catalogue{row("a", "A", "RATES"), row("b", "B", "FX"),
        row("c", "C", "RATES"), row("d", "D", "")};
    render::series_browser_view view;
    CHECK(render::update_asset_classes(view, 3, catalogue));
    REQUIRE(view.classes.size() == 2);
    CHECK(catalogue[view.classes[0]].asset_class == "FX");
    CHECK(catalogue[view.classes[1]].asset_class == "RATES");
    CHECK_FALSE(render::update_asset_classes(view, 3, catalogue));
    CHECK(render::update_asset_classes(view, 4, catalogue));
    CHECK(view.class_builds == 2);
}

TEST_CASE("the series view lists one provider when asked, and all when not",
    "[browser][view][provider]") {
    const std::vector catalogue{
        row("a", "EUR/USD", "FX", "1Y", "p1", "ore"),
        row("b", "EUR/USD", "FX", "1Y", "p1", "bloomberg"),
        row("c", "GBP/USD", "FX", "1Y", "p1", "bloomberg")};
    render::series_browser_view view;
    render::update_series_browser_view(view, query(1), catalogue);
    CHECK(ids(view, catalogue).size() == 3);

    CHECK(render::update_series_browser_view(view,
        query(1, {}, {}, {}, "bloomberg"), catalogue));
    CHECK(ids(view, catalogue) == strings{"b", "c"});

    CHECK(render::update_series_browser_view(view,
        query(1, {}, {}, {}, "ore"), catalogue));
    CHECK(ids(view, catalogue) == strings{"a"});

    CHECK_FALSE(render::update_series_browser_view(view,
        query(1, {}, {}, {}, "ore"), catalogue));
}

TEST_CASE("the providers are listed once, in order, and kept per generation",
    "[browser][provider]") {
    const std::vector catalogue{
        row("a", "A", "FX", "1Y", "p1", "ore"),
        row("b", "B", "FX", "1Y", "p1", "bloomberg"),
        row("c", "C", "FX", "1Y", "p2", "bloomberg"),
        row("d", "D", "FX", "1Y", "p1", "")};
    render::series_browser_view view;
    CHECK(render::update_providers(view, 1, catalogue));
    REQUIRE(view.providers.size() == 2);
    CHECK(catalogue[view.providers[0]].provider_code == "bloomberg");
    CHECK(catalogue[view.providers[1]].provider_code == "ore");
    CHECK_FALSE(render::update_providers(view, 1, catalogue));
    CHECK(render::update_providers(view, 2, catalogue));
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
