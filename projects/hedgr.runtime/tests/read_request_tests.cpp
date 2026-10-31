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
#include "hedgr.runtime/worker/read_request.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace worker = hedgr::runtime::worker;
namespace session = hedgr::session::transport;

using fake_link = hedgr::testing::fake_session_link;

model::domain::session_key key() {
    return model::domain::session_key{.instance_id = "i-1",
        .account_id = "a-1"};
}

}

TEST_CASE("an image request names the event it will answer with", "[read]") {
    const worker::read_request request =
        worker::image_request{.image_id = "img-1"};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::image_loaded);
    CHECK(fact.instance_id == "i-1");
    CHECK(fact.account_id == "a-1");
    CHECK(fact.tenant_id == "t-1");
    CHECK(fact.image_id == "img-1");
}

TEST_CASE("an image map request names its event", "[read]") {
    const worker::read_request request = worker::image_map_request{};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::currency_image_map);
    CHECK(fact.tenant_id == "t-1");
    CHECK(fact.image_id.empty());
}

TEST_CASE("a CRM names request names its party", "[read]") {
    const worker::read_request request =
        worker::crm_names_request{.party_id = "p-1"};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::crm_names);
    CHECK(fact.party_id == "p-1");
}

TEST_CASE("a CRM rates request carries the scope it was asked for", "[read]") {
    const worker::read_request request = worker::crm_rates_request{
        .party_id = "p-1", .crm_name = "majors", .reciprocal = true};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::crm_rates);
    CHECK(fact.party_id == "p-1");
    CHECK(fact.crm_name == "majors");
    CHECK(fact.crm_reciprocal);
}

TEST_CASE("a curve request names the curve it will answer with", "[read]") {
    const worker::read_request request =
        worker::term_request{.key = "curve-1", .label = "USD swaps"};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::term_structure);
    CHECK(fact.curve_key == "curve-1");
    CHECK(fact.curve_label == "USD swaps");
}

TEST_CASE("a read fills the event the request named", "[read]") {
    fake_link link;
    link.image.mime_type = "image/png";
    link.image.data = {std::uint8_t{7}, std::uint8_t{8}};

    const worker::read_request request =
        worker::image_request{.image_id = "img-1"};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.kind == model::event::event_kind::image_loaded);
    CHECK(fact.image_id == "img-1");
    CHECK(fact.mime_type == "image/png");
    CHECK(fact.bytes == std::vector<std::uint8_t>{7, 8});
    CHECK(fact.message.empty());
}

TEST_CASE("a read makes no call to the link's lifecycle", "[read]") {
    fake_link link;

    const worker::read_request request = worker::image_map_request{};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(link.connects == 0);
    CHECK(link.resumes == 0);
    CHECK(link.disconnects == 0);
}

TEST_CASE("a failed read fills the event the request named", "[read]") {
    fake_link link;
    link.names_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "names refused"};

    const worker::read_request request =
        worker::crm_names_request{.party_id = "p-1"};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.kind == model::event::event_kind::crm_names);
    CHECK(fact.party_id == "p-1");
    CHECK(fact.crm_names.empty());
    CHECK(fact.message == "names refused");
}

TEST_CASE("a CRM rates read fills the event's cells", "[read]") {
    fake_link link;
    link.rates = {session::session_client::crm_rate{.crm_name = "majors",
        .base_currency_code = "EUR",
        .quote_currency_code = "USD",
        .rate = 1.09,
        .status = "indicative",
        .as_of = "2026-01-02T03:04:05Z"}};

    const worker::read_request request = worker::crm_rates_request{
        .party_id = "p-1", .crm_name = "majors", .reciprocal = false};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    REQUIRE(fact.crm_cells.size() == 1);
    CHECK(fact.crm_cells[0].crm_name == "majors");
    CHECK(fact.crm_cells[0].rate == 1.09);
    CHECK(fact.crm_as_of_ns != 0);
    CHECK(fact.message.empty());
}

TEST_CASE("a curve read fills the event's curves", "[read][term]") {
    fake_link link;
    link.tails["s-1"] = {
        session::market_point{.at_ns = 100, .value = 1.5}};

    const worker::read_request request = worker::term_request{
        .key = "curve-1",
        .label = "USD swaps",
        .member_ids = {"s-1"},
        .member_tenors = {"1Y"}};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    REQUIRE(fact.term_curves.size() == 1);
    CHECK(fact.curve_key == "curve-1");
    REQUIRE(fact.term_curves[0].nodes.size() == 1);
    CHECK(fact.term_curves[0].nodes[0].tenor == "1Y");
    CHECK(fact.term_curves[0].nodes[0].value == 1.5);
}

TEST_CASE("a deployment request names its event", "[read]") {
    const worker::read_request request = worker::deployment_request{};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::deployment_snapshot);
    CHECK(fact.instance_id == "i-1");
}

TEST_CASE("a deployment read fills the snapshot", "[read]") {
    fake_link link;
    link.deployment_read.tenant_total = 3;
    link.deployment_read.in_service = 2;
    link.deployment_read.tenants.push_back(
        session::tenant_summary{.id = "t-1",
            .name = "First",
            .status = "active",
            .type = "application"});

    const worker::read_request request = worker::deployment_request{};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.deployment.tenant_total == 3);
    CHECK(fact.deployment.in_service == 2);
    REQUIRE(fact.deployment.tenants.size() == 1);
    CHECK(fact.deployment.tenants[0].name == "First");
    CHECK(fact.deployment.observed_at_ns != 0);
    CHECK(fact.deployment.failure.empty());
}

TEST_CASE("a refused deployment read names the read that failed", "[read]") {
    fake_link link;
    link.deployment_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "the deployment refused"};

    const worker::read_request request = worker::deployment_request{};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.deployment.failure == "the deployment refused");
    CHECK(fact.deployment.failing_read == "deployment");
    CHECK(fact.deployment.tenants.empty());
}

TEST_CASE("a request that could not run answers where its handlers look",
    "[read]") {
    const auto deployment = worker::read_failed(worker::deployment_request{},
        key(), "t-1", "the session stopped before the read ran");

    CHECK(deployment.kind == model::event::event_kind::deployment_snapshot);
    CHECK(deployment.message == "the session stopped before the read ran");
    CHECK(deployment.deployment.failure
        == "the session stopped before the read ran");
    CHECK(deployment.deployment.failing_read == "session");

    const auto names = worker::read_failed(
        worker::crm_names_request{.party_id = "p-1"}, key(), "t-1", "no link");

    CHECK(names.message == "no link");
    CHECK(names.party_id == "p-1");
}

TEST_CASE("a catalogue request names its event", "[read]") {
    const worker::read_request request = worker::catalogue_request{};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::series_catalogue);
    CHECK(fact.series_id.empty());
}

TEST_CASE("a points request names its series", "[read]") {
    const worker::read_request request =
        worker::market_points_request{.series_id = "s-1"};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::series_points);
    CHECK(fact.series_id == "s-1");
}

TEST_CASE("a history request carries its span", "[read]") {
    const worker::read_request request = worker::market_history_request{
        .series_id = "s-1", .from_ns = 5, .to_ns = 9};

    const auto fact = worker::read_event(request, key(), "t-1");
    CHECK(fact.kind == model::event::event_kind::series_history);
    CHECK(fact.series_id == "s-1");
    CHECK(fact.history_from_ns == 5);
}

TEST_CASE("a points read fills the event's points", "[read][market]") {
    fake_link link;
    link.points_read.points = {
        session::market_point{.at_ns = 10, .value = 1.5},
        session::market_point{.at_ns = 20, .value = 1.6}};

    const worker::read_request request =
        worker::market_points_request{.series_id = "s-1"};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.kind == model::event::event_kind::series_points);
    REQUIRE(fact.points.size() == 2);
    CHECK(fact.points[1].value == 1.6);
    CHECK(fact.message.empty());
}

TEST_CASE("a history read asks for the span the request carries",
    "[read][market]") {
    fake_link link;
    link.points_read.points = {session::market_point{.at_ns = 10, .value = 1.0}};

    const worker::read_request request = worker::market_history_request{
        .series_id = "s-1", .from_ns = 5, .to_ns = 9};
    auto fact = worker::read_event(request, key(), "t-1");
    worker::run_read(link, request, fact);

    CHECK(fact.kind == model::event::event_kind::series_history);
    CHECK(fact.history_from_ns == 5);
    CHECK(fact.points.size() == 1);
}
