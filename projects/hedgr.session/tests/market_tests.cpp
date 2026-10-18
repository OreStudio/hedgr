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
#include "ores.marketdata.api/messaging/operations_protocol.hpp"
#include "ores.nats/domain/wire_codec.hpp"
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("a tick payload decodes to its time and value", "[tick]") {
    using ores::marketdata::messaging::market_tick;
    using std::chrono::seconds;

    market_tick payload;
    payload.oresmd_uri = "oresmd://fx/GBP?ccy=USD";
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
        payload.value = text;
        payload.observation_time =
            std::chrono::system_clock::time_point{seconds{1'700'000'000}};
        const auto encoded = ores::nats::default_wire_codec().encode(payload);

        REQUIRE_FALSE(hedgr::session::market::decode_tick(encoded).has_value());
    }

    REQUIRE_FALSE(
        hedgr::session::market::decode_tick({}).has_value());
}
