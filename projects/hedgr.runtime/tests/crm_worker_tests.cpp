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
#include "hedgr.model/command/channel.hpp"
#include "hedgr.runtime/worker/crm_worker.hpp"
#include "hedgr.runtime/worker/session_task.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "fake_link.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>

using hedgr::runtime::worker::parse_iso8601_utc;

namespace {

namespace session = hedgr::session::transport;
namespace worker = hedgr::runtime::worker;
namespace model = hedgr::model;

using fake_link = hedgr::testing::fake_session_link;

session::session_client::crm_rate rate(std::string base, std::string quote,
    double value, std::string as_of) {
    session::session_client::crm_rate row;
    row.crm_name = "majors";
    row.base_currency_code = std::move(base);
    row.quote_currency_code = std::move(quote);
    row.rate = value;
    row.status = "fresh";
    row.as_of = std::move(as_of);
    return row;
}

}

TEST_CASE("a CRM batch answer fills the event's fields", "[crm][link]") {
    fake_link link;
    link.rates = {rate("EUR", "USD", 1.1, "2026-10-18T21:52:25Z"),
        rate("GBP", "USD", 1.3, "2026-10-18T21:52:26Z")};
    link.conventions = {
        session::session_client::pair_convention{.pair_code = "EUR/USD",
            .decimal_places = 5, .pip_factor = 0.0001, .tick_size = 0.5}};

    model::event::event fact;
    worker::run_crm_read(link, "p", "majors", false, fact);

    REQUIRE(fact.crm_cells.size() == 2);
    CHECK(fact.crm_cells[0].base_currency_code == "EUR");
    CHECK(fact.crm_cells[1].rate == 1.3);
    CHECK(fact.crm_as_of_ns == parse_iso8601_utc("2026-10-18T21:52:26Z"));
    REQUIRE(fact.crm_conventions.size() == 1);
    CHECK(fact.crm_conventions[0].pair_code == "EUR/USD");
    CHECK(fact.message.empty());
    CHECK(link.crm_calls == 1);
}

TEST_CASE("a refused CRM batch becomes the event's message", "[crm][link]") {
    fake_link link;
    link.rates_error = session::session_error{
        .code = session::session_errc::read_refused,
        .message = "the rates were refused"};

    model::event::event fact;
    worker::run_crm_read(link, "p", "majors", false, fact);
    CHECK(fact.crm_cells.empty());
    CHECK(fact.message == "the rates were refused");
}

TEST_CASE("a link that cannot connect posts the failure and stops there",
    "[crm][link]") {
    fake_link link;
    link.connect_error = session::session_error{
        .code = session::session_errc::unreachable,
        .message = "no broker"};

    boost::asio::io_context context;
    model::command::event_queue events;
    model::event::event fact;
    bool worked = false;
    auto work = [&worked](session::session_link&,
                    model::event::event&) { worked = true; };
    boost::asio::co_spawn(context,
        worker::run_session_read(link, worker::session_read{}, fact, work,
            events),
        boost::asio::detached);
    context.run();

    const auto got = events.drain();
    REQUIRE(got.size() == 1);
    CHECK(got[0].message == "no broker");
    CHECK_FALSE(worked);
    CHECK(link.crm_calls == 0);
}

TEST_CASE("a link that cannot resume posts the failure and stops there",
    "[crm][link]") {
    fake_link link;
    link.resume_error = session::session_error{
        .code = session::session_errc::session_expired,
        .message = "the token is dead"};

    boost::asio::io_context context;
    model::command::event_queue events;
    model::event::event fact;
    bool worked = false;
    auto work = [&worked](session::session_link&,
                    model::event::event&) { worked = true; };
    boost::asio::co_spawn(context,
        worker::run_session_read(link, worker::session_read{}, fact, work,
            events),
        boost::asio::detached);
    context.run();

    const auto got = events.drain();
    REQUIRE(got.size() == 1);
    CHECK(got[0].message == "the token is dead");
    CHECK_FALSE(worked);
    CHECK(link.connects == 1);
    CHECK(link.resumes == 1);
    CHECK(link.crm_calls == 0);
}

TEST_CASE("a CRM as-of reads in OreStudio's wire shape", "[crm]") {
    constexpr std::int64_t t_form = 1'792'360'345'000'000'000;
    CHECK(parse_iso8601_utc("2026-10-18T21:52:25Z") == t_form);

    /* The shape to_iso8601_utc writes: a space and six fraction digits. */
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25.250000Z")
        == t_form + 250'000'000);
    CHECK(parse_iso8601_utc("2026-10-18 21:52:25+00:00") == t_form);

    /* An offset or a missing designator is not misread as UTC. */
    CHECK(parse_iso8601_utc("2026-10-18T22:52:25+01:00") == 0);
    CHECK(parse_iso8601_utc("2026-10-18T21:52:25") == 0);
    CHECK(parse_iso8601_utc("") == 0);
    CHECK(parse_iso8601_utc("not a time") == 0);
}
