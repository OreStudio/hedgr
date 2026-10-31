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
#include "hedgr.runtime/worker/session_worker.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <stop_token>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace worker = hedgr::runtime::worker;
namespace transport = hedgr::session::transport;

using fake_link = hedgr::testing::fake_session_link;

/** @brief A job for a session asked to sign in with a typed password. */
worker::session_job sign_in_job() {
    worker::session_job job;
    job.key = domain::session_key{"i-1", "a-1"};
    job.credential.principal = "ops";
    job.credential.password = "hunter2";
    job.attempt = 7;
    job.party = std::make_shared<worker::party_switch>();
    return job;
}

/** @brief A sign-in as the instance would answer it. */
transport::session_account account() {
    transport::session_account value;
    value.token = "token-1";
    value.tenant_id = "tenant-1";
    value.tenant_name = "Tenant";
    value.party_id = "party-1";
    value.party_name = "Party";
    value.principal = "ops";
    value.access_lifetime_s = 1800;
    return value;
}

/** @brief Runs @p context until @p done holds, or five seconds pass. */
template <class Predicate>
bool run_until(boost::asio::io_context& context, Predicate done) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        if (done())
            return true;
        /*
         * A run that finds no work leaves the context stopped, and a coroutine
         * that is about to post would then never be resumed.
         */
        context.restart();
        context.run_for(std::chrono::milliseconds{5});
    }
    return false;
}

}

TEST_CASE("a session signs in over the link it is given", "[session][link]") {
    fake_link link;
    link.account_value = account();

    hedgr::model::command::event_queue events;
    auto stop = std::make_shared<std::stop_source>();

    boost::asio::io_context context;
    boost::asio::co_spawn(context,
        worker::run_session_on_link(link, sign_in_job(), events, stop),
        boost::asio::detached);

    std::vector<event::event> facts;
    const auto signed_in = run_until(context, [&] {
        facts = events.drain();
        for (const auto& fact : facts) {
            if (fact.kind == event::event_kind::session_authenticated)
                return true;
        }
        return false;
    });
    REQUIRE(signed_in);

    /* The credential the operator typed is what reached the instance. */
    CHECK(link.connects == 1);
    CHECK(link.authentications == 1);
    CHECK(link.last_principal == "ops");
    CHECK(link.last_password == "hunter2");

    const auto authenticated = [&facts]() -> event::event {
        for (const auto& fact : facts) {
            if (fact.kind == event::event_kind::session_authenticated)
                return fact;
        }
        return {};
    }();
    CHECK(authenticated.instance_id == "i-1");
    CHECK(authenticated.account_id == "a-1");
    CHECK(authenticated.attempt == 7);
    CHECK(authenticated.tenant_id == "tenant-1");
    CHECK(authenticated.party_id == "party-1");
    CHECK(authenticated.token.view() == "token-1");

    /* A signed-in session reports the status it reached. */
    bool authenticated_status = false;
    bool connecting_status = false;
    for (const auto& fact : facts) {
        if (fact.kind != event::event_kind::session_status_changed)
            continue;
        connecting_status |= fact.status == domain::session_status::connecting;
        authenticated_status |=
            fact.status == domain::session_status::authenticated;
    }
    CHECK(connecting_status);
    CHECK(authenticated_status);

    /* Asking it to stop closes the session and reports the move. */
    stop->request_stop();
    const auto stopped = run_until(context, [&] {
        const auto rest = events.drain();
        facts.insert(facts.end(), rest.begin(), rest.end());
        for (const auto& fact : facts) {
            if (fact.kind == event::event_kind::session_status_changed
                && fact.status == domain::session_status::disconnected)
                return true;
        }
        return false;
    });
    CHECK(stopped);
}

TEST_CASE("a refused sign-in reports the refusal and stops", "[session][link]") {
    fake_link link;
    link.authenticate_error = transport::session_error{
        .code = transport::session_errc::authentication_rejected,
        .message = "the password is wrong"};

    hedgr::model::command::event_queue events;
    auto stop = std::make_shared<std::stop_source>();

    boost::asio::io_context context;
    boost::asio::co_spawn(context,
        worker::run_session_on_link(link, sign_in_job(), events, stop),
        boost::asio::detached);

    std::vector<event::event> facts;
    const auto refused = run_until(context, [&] {
        facts = events.drain();
        for (const auto& fact : facts) {
            if (fact.kind == event::event_kind::session_failed)
                return true;
        }
        return false;
    });
    REQUIRE(refused);

    const auto failure = [&facts]() -> event::event {
        for (const auto& fact : facts) {
            if (fact.kind == event::event_kind::session_failed)
                return fact;
        }
        return {};
    }();
    CHECK(failure.message == "the password is wrong");
    CHECK(failure.code == "authentication_rejected");

    /* The password reached the instance and was not signalled as a success. */
    CHECK(link.authentications == 1);
    CHECK(link.last_password == "hunter2");
}
