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
#include "hedgr.session/market/series_label.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "ores.marketdata.api/datum/market_datum.hpp"
#include "ores.marketdata.api/messaging/operations_protocol.hpp"
#include "ores.marketdata.core/datum/oresmd_uri_codec.hpp"
#include "ores.nats/domain/wire_codec.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>

namespace market = hedgr::session::market;

TEST_CASE("a series label names the parts its URI carries", "[market]") {
    /*
     * The deployment names a series by URI. These are the shapes the live
     * deployment writes: an FX spot pair, an interest-rate swap tenor, and a
     * zero curve named by its curve id.
     */
    REQUIRE(market::series_label(
                "oresmd://fx/GBP?type=series&instrument=fx_spot&quote=rate&ccy=USD")
        == "FX GBP/USD");
    REQUIRE(market::series_label(
                "oresmd://fx/USD?type=series&instrument=fx_spot&quote=rate&ccy=JPY")
        == "FX USD/JPY");
    REQUIRE(market::series_label(
                "oresmd://ir/GBP?type=series&instrument=ir_swap&quote=rate&fwd_start=0D&tenor=1D")
        == "IR GBP 1D");
    REQUIRE(market::series_label(
                "oresmd://ir/HUF?type=series&instrument=zero&quote=rate&curve_id=HUF1D&day_counter=A365")
        == "IR HUF HUF1D");

    /*
     * A URI this build does not understand is returned whole rather than
     * guessed at, because a wrong name is worse than a long one.
     */
    REQUIRE(market::series_label("not-a-uri") == "not-a-uri");
    REQUIRE(market::series_label("") == "");
}

TEST_CASE("a curve key groups the tenors of one rate series", "[market]") {
    const auto one_day =
        "oresmd://ir/GBP?type=series&instrument=ir_swap&quote=rate&fwd_start=0D&tenor=1D";
    const auto six_months =
        "oresmd://ir/GBP?type=series&instrument=ir_swap&quote=rate&fwd_start=0D&tenor=6M";
    const auto zero =
        "oresmd://ir/HUF?type=series&instrument=zero&quote=rate&curve_id=HUF1D&day_counter=A365";

    REQUIRE(market::series_term(one_day) == "1D");
    REQUIRE(market::series_term(six_months) == "6M");
    REQUIRE(market::series_term(zero).empty());
    REQUIRE(market::series_term(
                "oresmd://ir/INR?type=quote&instrument=ir_swap&tenor=1D&term=10Y")
        == "10Y");
    REQUIRE(market::series_curve_key(one_day) == market::series_curve_key(six_months));
    REQUIRE(market::series_curve_key(one_day)
        == "oresmd://ir/GBP?fwd_start=0D&instrument=ir_swap&quote=rate&type=series");
    REQUIRE(market::series_curve_key(zero).empty());
    REQUIRE(market::series_curve_label(one_day) == "IR GBP IR_SWAP");
}

TEST_CASE("a series asset class is the URI's own word, upper-cased",
    "[market]") {
    REQUIRE(market::series_asset_class("oresmd://fx/GBP?ccy=USD") == "FX");
    REQUIRE(market::series_asset_class("oresmd://ir/GBP?tenor=1D") == "IR");
    REQUIRE(market::series_asset_class("not-a-uri").empty());
}

namespace {

/*
 * The quote URI a feed publishes for one point of a series, made the way the
 * server makes it: through the datum model and the codec, never by editing
 * the series' text.
 */
std::string quote_uri_of(std::string_view series_uri, std::string_view point) {
    using ores::marketdata::datum::oresmd_uri_codec;
    const auto series = oresmd_uri_codec::read(series_uri);
    REQUIRE(series.has_value());
    const auto quote = point.empty()
        ? ores::marketdata::datum::datum_at(*series, {})
        : ores::marketdata::datum::datum_at_point(*series, point);
    REQUIRE(quote.has_value());
    const auto written = oresmd_uri_codec::write(*quote);
    REQUIRE(written.has_value());
    return *written;
}

std::vector<std::byte> tick_payload(std::string quote_uri) {
    ores::marketdata::messaging::market_tick payload;
    payload.oresmd_uri = std::move(quote_uri);
    payload.value = "1.5";
    payload.observation_time = std::chrono::system_clock::time_point{
        std::chrono::seconds{1'700'000'000}};
    return ores::nats::default_wire_codec().encode(payload);
}

}

TEST_CASE("a tick names the catalogue series its quote belongs to", "[tick]") {
    using hedgr::session::market::canonical_series_uri;
    using hedgr::session::market::decode_tick;

    /*
     * A spot pair has no coordinate, so its one quote is the series with the
     * type changed. A swap and a zero curve publish one quote per term, and
     * the term is on the quote only. Every family must reach its row.
     */
    const struct {
        const char* series;
        const char* point;
    } cases[] = {
        {"oresmd://fx/GBP?type=series&instrument=fx_spot&quote=rate&ccy=USD", ""},
        {"oresmd://ir/GBP?type=series&instrument=ir_swap&quote=rate&fwd_start=0D&tenor=1D",
            "10Y"},
        {"oresmd://ir/HUF?type=series&instrument=zero&quote=rate&curve_id=HUF1D&day_counter=A365",
            "5Y"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.series, c.point);
        const auto key = canonical_series_uri(c.series);
        REQUIRE(key.has_value());
        CHECK(*key == c.series);

        const auto decoded = decode_tick(tick_payload(quote_uri_of(c.series, c.point)));
        REQUIRE(decoded.has_value());
        CHECK(decoded->series_uri == *key);
    }

    /*
     * Two terms of one curve are two quotes of one series, and two curves
     * are not.
     */
    const auto swap = cases[1].series;
    CHECK(decode_tick(tick_payload(quote_uri_of(swap, "2Y")))->series_uri
        == decode_tick(tick_payload(quote_uri_of(swap, "30Y")))->series_uri);
    CHECK(decode_tick(tick_payload(quote_uri_of(swap, "2Y")))->series_uri
        != decode_tick(tick_payload(quote_uri_of(cases[2].series, "2Y")))->series_uri);

    CHECK_FALSE(canonical_series_uri("not-a-uri").has_value());
    CHECK_FALSE(decode_tick(tick_payload("not-a-uri")).has_value());
}

TEST_CASE("a tick payload decodes to its time and value", "[tick]") {
    using ores::marketdata::messaging::market_tick;
    using std::chrono::seconds;

    market_tick payload;
    payload.oresmd_uri =
        "oresmd://fx/GBP?type=quote&instrument=fx_spot&quote=rate&ccy=USD";
    payload.value = "1.23456";
    payload.observation_time =
        std::chrono::system_clock::time_point{seconds{1'700'000'000}};

    const auto encoded = ores::nats::default_wire_codec().encode(payload);
    const auto decoded = hedgr::session::market::decode_tick(encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->at_ns == 1'700'000'000'000'000'000);
    REQUIRE(decoded->value == 1.23456);
}

TEST_CASE("a tick payload with no number is refused", "[tick]") {
    using ores::marketdata::messaging::market_tick;
    using std::chrono::seconds;

    for (const auto* text : {"", "abc", "nan"}) {
        market_tick payload;
        payload.oresmd_uri =
            "oresmd://fx/GBP?type=quote&instrument=fx_spot&quote=rate&ccy=USD";
        payload.value = text;
        payload.observation_time =
            std::chrono::system_clock::time_point{seconds{1'700'000'000}};
        const auto encoded = ores::nats::default_wire_codec().encode(payload);

        REQUIRE_FALSE(hedgr::session::market::decode_tick(encoded).has_value());
    }

    REQUIRE_FALSE(
        hedgr::session::market::decode_tick({}).has_value());
}
