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
#include "fake_link.hpp"
#include "hedgr.runtime/worker/crm_names_worker.hpp"
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.runtime/worker/image_worker.hpp"
#include "hedgr.runtime/worker/curve_worker.hpp"
#include "hedgr.runtime/worker/surface_worker.hpp"
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace worker = hedgr::runtime::worker;
namespace session = hedgr::session::transport;

using fake_link = hedgr::testing::fake_session_link;

}

TEST_CASE("an image answer fills the event's type and bytes", "[link]") {
    fake_link link;
    link.image.mime_type = "image/png";
    link.image.data = {std::uint8_t{1}, std::uint8_t{2}};

    model::event::event fact;
    worker::run_image_read(link, "img-1", fact);
    CHECK(fact.mime_type == "image/png");
    CHECK(fact.bytes == std::vector<std::uint8_t>{1, 2});
    CHECK(fact.message.empty());
    CHECK(link.image_calls == 1);
}

TEST_CASE("a missing image becomes the event's message", "[link]") {
    fake_link link;
    link.image_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "no such image"};

    model::event::event fact;
    worker::run_image_read(link, "img-1", fact);
    CHECK(fact.bytes.empty());
    CHECK(fact.message == "no such image");
}

TEST_CASE("the currency image map becomes the event's refs", "[link]") {
    fake_link link;
    link.images = {
        session::session_client::currency_image{.currency = "EUR",
            .image_id = "img-eur"},
        session::session_client::currency_image{.currency = "USD",
            .image_id = "img-usd"}};

    model::event::event fact;
    worker::run_image_map_read(link, fact);
    REQUIRE(fact.image_refs.size() == 2);
    CHECK(fact.image_refs[0].currency == "EUR");
    CHECK(fact.image_refs[1].image_id == "img-usd");
}

TEST_CASE("a party's CRM names become the event's list", "[link]") {
    fake_link link;
    link.names = {"majors", "minors"};

    model::event::event fact;
    worker::run_crm_names_read(link, "p", fact);
    CHECK(fact.crm_names == std::vector<std::string>{"majors", "minors"});
    CHECK(fact.message.empty());
    CHECK(link.names_calls == 1);
}

TEST_CASE("a refused CRM names read becomes the event's message", "[link]") {
    fake_link link;
    link.names_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "names refused"};

    model::event::event fact;
    worker::run_crm_names_read(link, "p", fact);
    CHECK(fact.crm_names.empty());
    CHECK(fact.message == "names refused");
}

TEST_CASE("a curve is built from each member's tail", "[link][term]") {
    fake_link link;
    link.tails["s-1"] = {
        session::market_point{.at_ns = 100, .value = 1.0},
        session::market_point{.at_ns = 200, .value = 1.1}};
    link.tails["s-2"] = {
        session::market_point{.at_ns = 100, .value = 2.0},
        session::market_point{.at_ns = 200, .value = 2.1}};

    model::event::event fact;
    worker::run_curve_read(link,
        worker::curve_request{.key = "c",
            .label = "IR GBP",
            .member_ids = {"s-1", "s-2"},
            .member_tenors = {"1Y", "2Y"}},
        fact);
    CHECK(fact.curve_label == "IR GBP");
    CHECK(fact.curve_pillars == std::vector<std::string>{"1Y", "2Y"});
    CHECK(fact.curve_as_of_ns == std::vector<std::int64_t>{100, 200});
    CHECK(fact.curve_values == std::vector<double>{1.0, 2.0, 1.1, 2.1});
    CHECK(fact.message.empty());
    CHECK(link.tail_calls == 2);
}

TEST_CASE("a failed pillar becomes the event's message", "[link][term]") {
    fake_link link;
    link.tail_error = session::session_error{
        .code = session::session_errc::timeout, .message = "no answer"};

    model::event::event fact;
    worker::run_curve_read(link,
        worker::curve_request{.key = "c",
            .label = "IR GBP",
            .member_ids = {"s-1"},
            .member_tenors = {"1Y"}},
        fact);
    CHECK(fact.curve_pillars.empty());
    CHECK(fact.message == "no answer");
}

TEST_CASE("a curve takes the shape the deployment states for it",
    "[link][term]") {
    /*
     * An interest-rate series carries the ladder as one object with an axis per
     * term. The evolution read says so outright - its axes, every node on them,
     * and one value per node per instant - so nothing has to be inferred from a
     * tick reply, whose coordinates name no term.
     */
    fake_link link;
    link.evolution.fields = {"term"};
    link.evolution.nodes = {{"1Y"}, {"10Y"}};
    link.evolution.instants = {
        {.as_of_ns = 100, .values = {0.01, 0.03}},
        {.as_of_ns = 200, .values = {0.011, 0.031}}};

    model::event::event fact;
    worker::curve_request request{.key = "c", .label = "IR GBP"};
    request.member_ids = {"s-curve"};
    request.member_tenors = {""};
    request.member_identity = model::domain::market_series_identity{
        .asset = "ir", .scope = "GBP", .instrument_type = "ir_swap"};
    worker::run_curve_read(link, request, fact);
    CHECK(fact.curve_label == "IR GBP");
    CHECK(fact.curve_pillars == std::vector<std::string>{"1Y", "10Y"});
    CHECK(fact.curve_as_of_ns == std::vector<std::int64_t>{100, 200});
    CHECK(fact.curve_values == std::vector<double>{0.01, 0.03, 0.011, 0.031});
    CHECK(fact.message.empty());
    CHECK(link.tail_calls == 0);
}

TEST_CASE("a shape with no term axis is reported, not drawn",
    "[link][term]") {
    fake_link link;
    link.evolution.fields = {"strike"};
    link.evolution.nodes = {{"ATM"}};
    link.evolution.instants = {{.as_of_ns = 100, .values = {0.2}}};

    model::event::event fact;
    worker::curve_request request{.key = "c"};
    request.member_ids = {"s-curve"};
    request.member_tenors = {""};
    request.member_identity = model::domain::market_series_identity{
        .asset = "fx", .instrument_type = "fx_option"};
    worker::run_curve_read(link, request, fact);
    CHECK(fact.curve_pillars.empty());
    CHECK(fact.message.find("no term axis") != std::string::npos);
}

TEST_CASE("a published curve is read in one call, not one per tenor",
    "[link][term]") {
    fake_link link;
    link.curve_sets.push_back(session::curve_instant{
        .label = "EUR-ESTR",
        .series_id = "s-curve",
        .as_of_ns = 100,
        .state_code = "published",
        .points = {session::curve_point{.pillar = "2Y",
                       .value = 0.02, .numeric = true},
            session::curve_point{.pillar = "1Y",
                .value = 0.01, .numeric = true}}});

    model::event::event fact;
    worker::run_curve_read(link,
        worker::curve_request{.key = "s-curve", .label = "EUR-ESTR",
            .published = true},
        fact);
    CHECK(fact.curve_pillars == std::vector<std::string>{"2Y", "1Y"});
    CHECK(fact.curve_as_of_ns == std::vector<std::int64_t>{100});
    CHECK(fact.curve_values == std::vector<double>{0.02, 0.01});
    CHECK(fact.message.empty());
    CHECK(link.tail_calls == 0);
}

TEST_CASE("a published curve the deployment states empty is a failure",
    "[link][term]") {
    fake_link link;
    link.curve_sets.push_back(session::curve_instant{
        .label = "EUR-ESTR", .series_id = "s-curve", .as_of_ns = 100});

    model::event::event fact;
    worker::run_curve_read(link,
        worker::curve_request{.key = "s-curve", .published = true}, fact);
    CHECK(fact.curve_pillars.empty());
    CHECK_FALSE(fact.message.empty());
}

namespace {

model::event::event market_read(fake_link& link, worker::read_request request) {
    auto fact = worker::read_event(request,
        model::domain::session_key{"market-i", "a"}, {});
    worker::run_market_read(link, request, fact);
    return fact;
}

worker::read_request catalogue_request() {
    return worker::catalogue_request{};
}

worker::read_request points_request() {
    return worker::market_points_request{.series_id = "s-1"};
}

}

TEST_CASE("a catalogue read carries its series and providers", "[link][market]") {
    fake_link link;
    link.catalogue.push_back(session::market_series_summary{
        .id = "s-1", .label = "FX EUR/USD", .provider_code = "bloomberg"});
    link.providers.push_back(model::domain::market_provider_row{
        .code = "bloomberg", .name = "Bloomberg", .kind = "data_vendor"});

    const auto fact = market_read(link, catalogue_request());
    CHECK(fact.kind == model::event::event_kind::series_catalogue);
    REQUIRE(fact.series.size() == 1);
    CHECK(fact.series[0].label == "FX EUR/USD");
    REQUIRE(fact.providers.size() == 1);
    CHECK(fact.providers[0].kind == "data_vendor");
    CHECK(fact.message.empty());
}

TEST_CASE("a catalogue read carries the curves the deployment publishes",
    "[link][market][curve]") {
    fake_link link;
    link.curve_heads.push_back(model::domain::published_curve{
        .label = "EUR-ESTR", .series_id = "s-curve", .points = 7});

    const auto fact = market_read(link, catalogue_request());
    REQUIRE(fact.published_curves.size() == 1);
    CHECK(fact.published_curves[0].label == "EUR-ESTR");
    CHECK(fact.published_curves[0].series_id == "s-curve");
    CHECK(fact.published_curves[0].points == 7);
    CHECK(fact.published_curves_error.empty());
    CHECK(fact.message.empty());
}

TEST_CASE("a failed curve read leaves the catalogue standing",
    "[link][market][curve]") {
    fake_link link;
    link.catalogue.push_back(session::market_series_summary{
        .id = "s-1", .label = "FX EUR/USD"});
    link.curves_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "curves refused"};

    const auto fact = market_read(link, catalogue_request());
    CHECK(fact.message.empty());
    REQUIRE(fact.series.size() == 1);
    CHECK(fact.published_curves.empty());
    CHECK(fact.published_curves_error == "curves refused");
}

TEST_CASE("a catalogue read carries the instruments the deployment groups by",
    "[link][market][instrument]") {
    fake_link link;
    link.catalogue.push_back(session::market_series_summary{
        .id = "s-curve", .label = "IR EUR EST R"});
    link.instruments.push_back(model::domain::market_instrument{
        .instrument_id = "i-curve", .kind = "curve", .name = "EUR-ESTR",
        .currency = "EUR", .instrument_type = "discount",
        .quote_type = "rate",
        .axes = {{.field = "term", .values = {"3M", "6M", "12M"}}},
        .sources = {{.provider_code = "ore",
            .provider_kind = "reference", .series_id = "s-curve",
            .as_of_ns = 1}},
        .party_id = "p1"});

    const auto fact = market_read(link, catalogue_request());
    CHECK(fact.kind == model::event::event_kind::series_catalogue);
    CHECK(fact.message.empty());
    /* The series still travel: a chart is interned from them, not from a row. */
    REQUIRE(fact.series.size() == 1);
    REQUIRE(fact.instruments.size() == 1);
    const auto& instrument = fact.instruments.front();
    CHECK(instrument.name == "EUR-ESTR");
    CHECK(instrument.kind == "curve");
    CHECK(instrument.currency == "EUR");
    REQUIRE(instrument.axes.size() == 1);
    CHECK(instrument.axes[0].field == "term");
    CHECK(instrument.axes[0].values.size() == 3);
    CHECK(instrument.sources.size() == 1);
    CHECK(instrument.sources[0].series_id == "s-curve");
    CHECK(fact.instruments_error.empty());
}

TEST_CASE("a failed instrument read leaves the catalogue standing and says why",
    "[link][market][instrument]") {
    fake_link link;
    link.catalogue.push_back(session::market_series_summary{
        .id = "s-1", .label = "FX EUR/USD"});
    link.instruments_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "instruments refused"};

    const auto fact = market_read(link, catalogue_request());
    CHECK(fact.message.empty());
    REQUIRE(fact.series.size() == 1);
    CHECK(fact.instruments.empty());
    CHECK(fact.instruments_error == "instruments refused");
}

TEST_CASE("a ladder read carries the rungs and the conventions they rest on",
    "[link][market][ladder]") {
    fake_link link;
    auto& held = link.ladder;
    held.currency = "AUD";
    held.instrument_type = "discount";
    held.as_of_ns = 1'760'000'000'000'000'000;
    held.provider_count = 1;
    held.tenor_convention = "RATES_SPOT_FORWARD";
    held.day_count_convention = "A365F";
    held.rungs = {
        {.tenor = "3M", .maturity = "2027-02-14", .year_fraction = 0.2521,
            .quotes = {{.provider_code = "ore", .value = 0.043015,
                .as_of_ns = 1'760'000'000'000'000'000, .age_seconds = 401}},
            .consensus = {.value = 0.042931, .method = "median",
                .providers_used = 13}}};

    const auto fact = market_read(link, worker::rate_ladder_request{
        .currency = "AUD", .instrument_type = "discount", .key = "aud|discount|"});
    CHECK(fact.kind == model::event::event_kind::rate_ladder);
    CHECK(fact.ladder_key == "aud|discount|");
    CHECK(fact.message.empty());
    REQUIRE(fact.ladder.rungs.size() == 1);
    const auto& rung = fact.ladder.rungs.front();
    CHECK(rung.tenor == "3M");
    CHECK(rung.maturity == "2027-02-14");
    CHECK(rung.year_fraction == 0.2521);
    CHECK(rung.consensus.method == "median");
    CHECK(rung.consensus.providers_used == 13);
    REQUIRE(rung.quotes.size() == 1);
    CHECK(rung.quotes.front().provider_code == "ore");
    CHECK(fact.ladder.tenor_convention == "RATES_SPOT_FORWARD");
    CHECK(fact.ladder.day_count_convention == "A365F");
}

TEST_CASE("a failed ladder read becomes the event's message",
    "[link][market][ladder]") {
    fake_link link;
    link.ladder_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "no ladder for AUD"};
    const auto fact = market_read(link, worker::rate_ladder_request{
        .currency = "AUD", .instrument_type = "discount", .key = "aud|discount|"});
    CHECK(fact.kind == model::event::event_kind::rate_ladder);
    CHECK(fact.ladder.rungs.empty());
    CHECK(fact.message == "no ladder for AUD");
}

TEST_CASE("a refused surface read says what it means and why",
    "[link][market][surface]") {
    /*
     * The deployment's own sentence is the only one an operator can act on:
     * "nothing is published at or before this instant" is a fact about the
     * desk, while "the read was refused" is not. The event carries both.
     */
    fake_link link;
    link.surface_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "the deployment refused the read",
        .detail = "No surface of EURUSD_SSVI is published at or before this "
                  "instant."};
    const auto request = worker::surface_request{.blueprint_id = "bp-1",
        .label = "EURUSD_SSVI"};
    auto fact = worker::read_event(request,
        model::domain::session_key{"market-i", "a"}, {});
    worker::run_surface_read(link, request, fact);
    CHECK(fact.kind == model::event::event_kind::vol_surface);
    CHECK(fact.message
        == "the deployment refused the read: No surface of EURUSD_SSVI is "
           "published at or before this instant.");
}

TEST_CASE("a points read carries its points", "[link][market]") {
    fake_link link;
    link.points_read.points = {
        session::market_point{.at_ns = 10, .value = 1.5},
        session::market_point{.at_ns = 20, .value = 1.6}};

    const auto fact = market_read(link, points_request());
    CHECK(fact.kind == model::event::event_kind::series_points);
    REQUIRE(fact.points.size() == 2);
    CHECK(fact.points[1].value == 1.6);
}

TEST_CASE("a failed points read becomes the event's message", "[link][market]") {
    fake_link link;
    link.points_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "the read was refused"};

    const auto fact = market_read(link, points_request());
    CHECK(fact.points.empty());
    CHECK(fact.message == "the read was refused");
}

TEST_CASE("the tick stream connects and resumes through the link", "[link][tick]") {
    fake_link link;
    const ores::nats::wire_codec codec{ores::nats::wire_format::msgpack};
    hedgr::session::market::market_tick_stream stream(link, "token",
        "principal", "tenant", "party", codec);
    CHECK_FALSE(stream.connected());

    CHECK(stream.connect());
    CHECK(stream.connected());
    CHECK(link.connects == 1);
    CHECK(link.resumes == 1);

    stream.disconnect();
    CHECK_FALSE(stream.connected());
    CHECK(link.disconnects == 1);
}
