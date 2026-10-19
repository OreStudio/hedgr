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
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/async/executor.hpp"
#include "hedgr.runtime/service/pending_reads.hpp"
#include "hedgr.runtime/service/read_service.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace service = hedgr::runtime::service;
namespace state = hedgr::model::state;

using service::request_id;
using service::request_kind;

constexpr auto instance_id = "inst-1";
constexpr auto account_id = "acct-1";
constexpr auto tenant_id = "tenant-1";

/**
 * @brief A read service over one authenticated session on a refused port.
 *
 * The service is built directly, with the tables it reads, so a test applies
 * exactly the answer it chooses. The reads it starts fail at once against the
 * refused port and push their own answers to @c events, which nothing drains.
 */
struct fixture final {
    fixture() {
        domain::instance row;
        row.id = instance_id;
        row.name = "Refused";
        row.nats_url = "nats://127.0.0.1:1";
        row.subject_prefix = "ores.test.refused";
        static_cast<void>(instances.upsert(row));

        session = sessions.insert(instance_id, account_id);
        sessions.set_account_result(session, "tenant", "party", "p-1",
            domain::secret_string{std::string{"a-token"}});
        sessions.set_tenant_id(session, tenant_id);
        sessions.set_status(session, domain::session_status::authenticated);

        domain::window_spec centre;
        centre.id = "command-centre";
        centre.kind = domain::window_kind::command_centre;
        centre.session = domain::session_key{instance_id, account_id};
        command_centre = windows.insert(centre);
        windows.bind_session(command_centre, session);

        domain::window_spec browser;
        browser.id = "series-browser";
        browser.kind = domain::window_kind::series_browser;
        browser.session = domain::session_key{instance_id, account_id};
        series_browser = windows.insert(browser);
        windows.bind_session(series_browser, session);
    }

    ~fixture() { pool.stop(); }

    void open() { reads.attach(std::filesystem::temp_directory_path()); }

    hedgr::model::command::event_queue events;
    state::instance_table instances;
    state::account_table accounts;
    state::session_table sessions;
    state::window_table windows;
    state::session_handle session;
    state::window_handle command_centre;
    state::window_handle series_browser;
    hedgr::session::transport::session_options seed;
    hedgr::runtime::async::executor pool{1};
    service::read_service reads{pool.context(), events, instances, accounts,
        sessions, windows, seed};
};

/** @brief One kind of read, how to start it and how it ends. */
struct read_case final {
    const char* name;
    bool needs_sign_in;
    request_id id;
    std::function<void(fixture&)> start;
    event::event answer;
};

[[nodiscard]] event::event answer_of(event::event_kind kind) {
    event::event fact;
    fact.kind = kind;
    fact.instance_id = instance_id;
    fact.account_id = account_id;
    fact.tenant_id = tenant_id;
    return fact;
}

[[nodiscard]] std::vector<read_case> read_cases() {
    auto points = answer_of(event::event_kind::series_points);
    points.series_id = "s-1";
    auto image = answer_of(event::event_kind::image_loaded);
    image.image_id = "img-1";
    return {
        {"catalogue", true, request_id::catalogue(instance_id, account_id),
            [](fixture& f) {
                static_cast<void>(f.reads.start(
                    {service::read_kind::market_catalogue, f.series_browser}));
            },
            answer_of(event::event_kind::series_catalogue)},
        {"points", true, request_id::points(instance_id, account_id, "s-1"),
            [](fixture& f) {
                f.reads.start_market_backfill(instance_id, account_id, "s-1");
            },
            points},
        {"deployment", true, request_id::deployment(instance_id, account_id),
            [](fixture& f) {
                static_cast<void>(f.reads.start(
                    {service::read_kind::deployment, f.command_centre}));
            },
            answer_of(event::event_kind::deployment_snapshot)},
        {"image", true, request_id::image(tenant_id, "img-1"),
            [](fixture& f) {
                hedgr::model::command::command cmd;
                cmd.kind = hedgr::model::command::command_kind::load_image;
                cmd.instance_id = instance_id;
                cmd.account_id = account_id;
                cmd.image_id = "img-1";
                f.reads.load_image(cmd);
            },
            image},
        /* The map is read for a token and does not look at the session's status. */
        {"currency image map", false, request_id::currency_image_map(tenant_id),
            [](fixture& f) {
                f.reads.start_currency_image_map(
                    answer_of(event::event_kind::session_authenticated));
            },
            answer_of(event::event_kind::currency_image_map)},
    };
}

}

TEST_CASE("the registry marks a read once and clears it by id", "[reads][registry]") {
    service::pending_reads pending;
    const auto id = request_id::points(instance_id, account_id, "s-1");

    CHECK(pending.empty());
    CHECK(pending.mark(id));
    CHECK_FALSE(pending.mark(id));
    CHECK(pending.contains(id));
    CHECK_FALSE(pending.contains(request_id::points(instance_id, account_id, "s-2")));
    CHECK_FALSE(pending.contains(request_id::catalogue(instance_id, account_id)));

    pending.clear(id);
    CHECK_FALSE(pending.contains(id));
    CHECK(pending.empty());
}

TEST_CASE("an id of one kind never collides with another's", "[reads][registry]") {
    service::pending_reads pending;
    REQUIRE(pending.mark(request_id::catalogue(instance_id, account_id)));
    CHECK(pending.mark(request_id::deployment(instance_id, account_id)));
    CHECK(pending.subjects(request_kind::image).empty());

    REQUIRE(pending.mark(request_id::image(tenant_id, "img-1")));
    REQUIRE(pending.subjects(request_kind::image).size() == 1);
    CHECK(pending.subjects(request_kind::image).contains(
        state::make_tenant_key(tenant_id, "img-1")));

    pending.clear_all();
    CHECK(pending.empty());
}

TEST_CASE("exactly the five answering events name a read", "[reads][registry]") {
    using kind = event::event_kind;
    for (const auto answering : {kind::series_catalogue, kind::series_points,
             kind::deployment_snapshot, kind::image_loaded,
             kind::currency_image_map}) {
        CHECK(request_id::answered_by(answer_of(answering)).has_value());
    }
    for (const auto other : {kind::session_status_changed,
             kind::session_authenticated, kind::session_failed,
             kind::probe_result, kind::crm_names, kind::crm_rates,
             kind::term_structure, kind::window_data}) {
        CHECK_FALSE(request_id::answered_by(answer_of(other)).has_value());
    }
}

TEST_CASE("every terminal outcome clears the mark of a read", "[reads][registry]") {
    enum class outcome { success, failure, session_lost };
    for (const auto& scenario : read_cases()) {
        for (const auto how : {outcome::success, outcome::failure,
                 outcome::session_lost}) {
            CAPTURE(scenario.name, static_cast<int>(how));
            fixture f;
            f.open();
            scenario.start(f);
            REQUIRE(f.reads.pending().contains(scenario.id));

            if (how == outcome::session_lost)
                f.sessions.remove(f.session);
            auto answer = scenario.answer;
            if (how == outcome::failure)
                answer.message = "the read failed";
            static_cast<void>(f.reads.apply(answer));

            CHECK_FALSE(f.reads.pending().contains(scenario.id));
            CHECK(f.reads.pending().empty());
        }
    }
}

TEST_CASE("a read the session cannot make is refused without a mark",
    "[reads][registry]") {
    enum class refusal_case { signed_out, no_token, store_closed };
    for (const auto& scenario : read_cases()) {
        for (const auto why : {refusal_case::signed_out, refusal_case::no_token,
                 refusal_case::store_closed}) {
            CAPTURE(scenario.name, static_cast<int>(why));
            fixture f;
            if (why != refusal_case::store_closed)
                f.open();
            if (why == refusal_case::signed_out)
                f.sessions.set_status(f.session,
                    domain::session_status::disconnected);
            if (why == refusal_case::no_token)
                f.sessions.set_token(f.session, domain::secret_string{});

            scenario.start(f);
            const bool refused = why != refusal_case::signed_out
                || scenario.needs_sign_in;
            CHECK(f.reads.pending().empty() == refused);
        }
    }
}

TEST_CASE("a stale answer clears nothing it does not name", "[reads][registry]") {
    fixture f;
    f.open();
    f.reads.start_market_backfill(instance_id, account_id, "s-1");
    REQUIRE(f.reads.pending().contains(
        request_id::points(instance_id, account_id, "s-1")));

    /* An answer for another series, and one for a read nobody started. */
    auto other = answer_of(event::event_kind::series_points);
    other.series_id = "s-2";
    static_cast<void>(f.reads.apply(other));
    auto catalogue = answer_of(event::event_kind::series_catalogue);
    static_cast<void>(f.reads.apply(catalogue));

    CHECK(f.reads.pending().contains(
        request_id::points(instance_id, account_id, "s-1")));
}

TEST_CASE("clearing the service forgets every mark", "[reads][registry]") {
    fixture f;
    f.open();
    for (const auto& scenario : read_cases())
        scenario.start(f);
    REQUIRE_FALSE(f.reads.pending().empty());

    f.reads.clear();
    CHECK(f.reads.pending().empty());
}
