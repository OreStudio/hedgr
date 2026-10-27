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
