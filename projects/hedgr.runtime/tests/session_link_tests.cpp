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
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/term_structure_worker.hpp"
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
    const std::vector<std::string> ids{"s-1", "s-2"};
    const std::vector<std::string> tenors{"1Y", "2Y"};

    model::event::event fact;
    worker::run_term_structure_read(link, ids, tenors, fact);
    REQUIRE(fact.term_curves.size() == 2);
    CHECK(fact.term_curves[0].at_ns == 100);
    REQUIRE(fact.term_curves[0].nodes.size() == 2);
    CHECK(fact.term_curves[0].nodes[0].tenor == "1Y");
    CHECK(fact.term_curves[0].nodes[0].value == 1.0);
    CHECK(fact.term_curves[1].nodes[1].value == 2.1);
    CHECK(fact.message.empty());
    CHECK(link.tail_calls == 2);
}

TEST_CASE("a failed pillar becomes the event's message", "[link][term]") {
    fake_link link;
    link.tail_error = session::session_error{
        .code = session::session_errc::timeout, .message = "no answer"};
    const std::vector<std::string> ids{"s-1"};
    const std::vector<std::string> tenors{"1Y"};

    model::event::event fact;
    worker::run_term_structure_read(link, ids, tenors, fact);
    CHECK(fact.term_curves.empty());
    CHECK(fact.message == "no answer");
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
