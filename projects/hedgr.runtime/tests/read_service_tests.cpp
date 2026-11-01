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
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string_view>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
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

        domain::window_spec matrix;
        matrix.id = "crm-matrix";
        matrix.kind = domain::window_kind::crm_matrix;
        matrix.session = domain::session_key{instance_id, account_id};
        matrix.state = domain::crm_matrix_state{.party_id = "p-1",
            .crm_name = "majors",
            .reciprocal = false};
        crm_matrix = windows.insert(matrix);
        windows.bind_session(crm_matrix, session);

        domain::window_spec curve;
        curve.id = "term-structure";
        curve.kind = domain::window_kind::term_structure;
        curve.session = domain::session_key{instance_id, account_id};
        curve.state = domain::term_structure_state{.label = "Curve",
            .curve_key = "curve-1",
            .member_ids = {"m-1y", "m-2y"},
            .member_tenors = {"1Y", "2Y"}};
        term_table = windows.insert(curve);
        windows.bind_session(term_table, session);
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
    state::window_handle crm_matrix;
    state::window_handle term_table;
    hedgr::session::transport::session_options seed;
    hedgr::runtime::async::executor pool{1};
    hedgr::model::state::series_table series;

    /** @brief The reads handed to the session's actor, in order. */
    std::vector<hedgr::runtime::worker::read_request> submitted;

    /** @brief Whether the session refuses the next read, as a stopped one would. */
    bool refuse_reads = false;

    service::read_service reads{sessions, windows, series,
        [this](std::string_view, std::string_view,
               const hedgr::runtime::worker::read_request& request) {
            if (refuse_reads)
                return false;
            submitted.push_back(request);
            /*
             * The session's actor answers every read it accepts, so this one
             * does too: with the failure an unreachable instance gives. A test
             * that drains the queue sees the answer the frame loop would.
             */
            events.push(hedgr::runtime::worker::read_failed(request,
                domain::session_key{instance_id, account_id}, tenant_id,
                "the instance is not reachable"));
            return true;
        }};
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

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t hour = 3600 * second;
constexpr std::int64_t base = 1'800'000'000 * second;

/**
 * @brief The first backfill of a dense series: 2,000 one second observations.
 *
 * The fine level holds 33 minutes, which is the depth a one hour chart finds
 * short.
 */
[[nodiscard]] hedgr::model::state::series_handle seed_backfill(fixture& f) {
    const auto row = f.series.intern(instance_id, account_id, "s-1");
    std::vector<domain::market_point> points;
    for (int i = 0; i < 2000; ++i)
        points.push_back({.at_ns = base + i * second, .value = 1.0 + i});
    f.series.seed(row, points, 1);
    return row;
}

[[nodiscard]] hedgr::model::command::command history_command(
    std::int64_t span_ns) {
    hedgr::model::command::command cmd;
    cmd.kind = hedgr::model::command::command_kind::load_history;
    cmd.instance_id = instance_id;
    cmd.account_id = account_id;
    cmd.series_id = "s-1";
    cmd.history_span_ns = span_ns;
    return cmd;
}

/** @brief The answer a history read pushes, with @p count older observations. */
[[nodiscard]] event::event history_answer(std::int64_t from_ns, int count,
    bool truncated = false) {
    event::event fact;
    fact.kind = event::event_kind::series_history;
    fact.instance_id = instance_id;
    fact.account_id = account_id;
    fact.series_id = "s-1";
    fact.history_from_ns = from_ns;
    fact.history_truncated = truncated;
    fact.observed_at_ns = 2;
    for (int i = count; i > 0; --i)
        fact.points.push_back({.at_ns = base - i * second, .value = 0.5});
    return fact;
}

[[nodiscard]] std::vector<read_case> read_cases() {
    auto history = answer_of(event::event_kind::series_history);
    history.series_id = "s-1";
    auto points = answer_of(event::event_kind::series_points);
    points.series_id = "s-1";
    auto image = answer_of(event::event_kind::image_loaded);
    image.image_id = "img-1";
    return {
        {"history", true, request_id::history(instance_id, account_id, "s-1"),
            [](fixture& f) {
                static_cast<void>(seed_backfill(f));
                f.reads.load_history(history_command(hour));
            },
            history},
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
                f.reads.start_currency_image_map(instance_id, account_id,
                    tenant_id);
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

TEST_CASE("exactly the six answering events name a read", "[reads][registry]") {
    using kind = event::event_kind;
    for (const auto answering : {kind::series_catalogue, kind::series_points,
             kind::series_history, kind::deployment_snapshot, kind::image_loaded,
             kind::currency_image_map}) {
        CHECK(request_id::answered_by(answer_of(answering)).has_value());
    }
    for (const auto other : {kind::session_status_changed,
             kind::session_authenticated, kind::session_failed,
             kind::probe_result, kind::crm_names, kind::crm_rates,
             kind::term_structure}) {
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

TEST_CASE("an image read is handed to the session's actor", "[reads][actor]") {
    fixture f;
    f.open();
    hedgr::model::command::command cmd;
    cmd.kind = hedgr::model::command::command_kind::load_image;
    cmd.instance_id = instance_id;
    cmd.account_id = account_id;
    cmd.image_id = "img-1";

    f.reads.load_image(cmd);

    REQUIRE(f.submitted.size() == 1);
    const auto* request =
        std::get_if<hedgr::runtime::worker::image_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->image_id == "img-1");
    CHECK(f.reads.pending().contains(request_id::image(tenant_id, "img-1")));
}

TEST_CASE("the currency image map is handed to the session's actor",
    "[reads][actor]") {
    fixture f;
    f.open();

    f.reads.start_currency_image_map(instance_id, account_id, tenant_id);

    REQUIRE(f.submitted.size() == 1);
    CHECK(std::holds_alternative<hedgr::runtime::worker::image_map_request>(
        f.submitted[0]));
    CHECK(f.reads.pending().contains(
        request_id::currency_image_map(tenant_id)));
}

TEST_CASE("a CRM batch is handed to the session's actor", "[reads][actor]") {
    fixture f;
    f.open();

    const auto refused = f.reads.start(
        {service::read_kind::crm_batch, f.crm_matrix});
    CHECK_FALSE(refused.has_value());

    REQUIRE(f.submitted.size() == 1);
    const auto* request =
        std::get_if<hedgr::runtime::worker::crm_rates_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->party_id == "p-1");
    CHECK(request->crm_name == "majors");
    CHECK_FALSE(request->reciprocal);

    const auto* batch = f.reads.crm().get(instance_id, account_id, "p-1",
        "majors", false);
    REQUIRE(batch != nullptr);
    CHECK(batch->reading);
}

TEST_CASE("the CRM names are handed to the session's actor",
    "[reads][actor]") {
    fixture f;
    f.open();

    hedgr::model::command::command cmd;
    cmd.kind = hedgr::model::command::command_kind::load_crm_names;
    cmd.instance_id = instance_id;
    cmd.account_id = account_id;
    cmd.party_id = "p-1";
    f.reads.load_crm_names(cmd);

    REQUIRE(f.submitted.size() == 1);
    const auto* request =
        std::get_if<hedgr::runtime::worker::crm_names_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->party_id == "p-1");
}

TEST_CASE("a deployment read is handed to the session's actor",
    "[reads][actor]") {
    fixture f;
    f.open();

    const auto refused = f.reads.start(
        {service::read_kind::deployment, f.command_centre});
    CHECK_FALSE(refused.has_value());

    REQUIRE(f.submitted.size() == 1);
    CHECK(std::holds_alternative<hedgr::runtime::worker::deployment_request>(
        f.submitted[0]));
    CHECK(f.reads.pending().contains(
        request_id::deployment(instance_id, account_id)));
}

TEST_CASE("a curve read is handed to the session's actor", "[reads][actor]") {
    fixture f;
    f.open();

    const auto refused = f.reads.start(
        {service::read_kind::term_structure, f.term_table});
    CHECK_FALSE(refused.has_value());

    REQUIRE(f.submitted.size() == 1);
    const auto* request =
        std::get_if<hedgr::runtime::worker::term_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->key == "curve-1");
    CHECK(request->label == "Curve");
    CHECK(request->member_ids == std::vector<std::string>{"m-1y", "m-2y"});
    CHECK(request->member_tenors == std::vector<std::string>{"1Y", "2Y"});
    CHECK(f.reads.term_structures().get(instance_id, account_id, "curve-1")
        != nullptr);
}

TEST_CASE("a catalogue read is handed to the session's actor",
    "[reads][actor]") {
    fixture f;
    f.open();

    static_cast<void>(f.reads.start(
        {service::read_kind::market_catalogue, f.series_browser}));

    REQUIRE(f.submitted.size() == 1);
    CHECK(std::holds_alternative<
        hedgr::runtime::worker::catalogue_request>(f.submitted[0]));
    CHECK(f.reads.pending().contains(
        request_id::catalogue(instance_id, account_id)));
}

TEST_CASE("a points read is handed to the session's actor", "[reads][actor]") {
    fixture f;
    f.open();

    f.reads.start_market_backfill(instance_id, account_id, "s-1");

    REQUIRE(f.submitted.size() == 1);
    const auto* request = std::get_if<
        hedgr::runtime::worker::market_points_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->series_id == "s-1");
    CHECK(f.reads.pending().contains(
        request_id::points(instance_id, account_id, "s-1")));
}

TEST_CASE("a history read is handed to the session's actor with its span",
    "[reads][actor]") {
    fixture f;
    f.open();
    static_cast<void>(seed_backfill(f));

    f.reads.load_history(history_command(hour));

    REQUIRE(f.submitted.size() == 1);
    const auto* request = std::get_if<
        hedgr::runtime::worker::market_history_request>(&f.submitted[0]);
    REQUIRE(request != nullptr);
    CHECK(request->series_id == "s-1");
    CHECK(request->from_ns == base + 1999 * second - hour);
    CHECK(request->to_ns > request->from_ns);
    CHECK(f.reads.pending().contains(
        request_id::history(instance_id, account_id, "s-1")));
}

TEST_CASE("a read the actor refuses leaves nothing in flight",
    "[reads][actor]") {
    fixture f;
    f.open();
    f.refuse_reads = true;

    hedgr::model::command::command cmd;
    cmd.kind = hedgr::model::command::command_kind::load_image;
    cmd.instance_id = instance_id;
    cmd.account_id = account_id;
    cmd.image_id = "img-1";
    f.reads.load_image(cmd);

    CHECK(f.submitted.empty());
    CHECK(f.reads.pending().empty());
    CHECK(f.reads.failed_images().empty());

    f.reads.start_currency_image_map(instance_id, account_id, tenant_id);

    CHECK(f.submitted.empty());
    CHECK(f.reads.pending().empty());
    CHECK(f.reads.currency_images().get(tenant_id) == nullptr);

    static_cast<void>(f.reads.start(
        {service::read_kind::crm_batch, f.crm_matrix}));

    CHECK(f.submitted.empty());
    CHECK(f.reads.crm().get(instance_id, account_id, "p-1", "majors", false)
        == nullptr);

    static_cast<void>(f.reads.start(
        {service::read_kind::deployment, f.command_centre}));

    CHECK(f.submitted.empty());
    CHECK(f.reads.pending().empty());

    static_cast<void>(f.reads.start(
        {service::read_kind::term_structure, f.term_table}));

    CHECK(f.submitted.empty());
    CHECK(f.reads.term_structures().get(instance_id, account_id, "curve-1")
        == nullptr);
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

TEST_CASE("a period the series already holds starts no read", "[reads][history]") {
    fixture f;
    f.open();
    static_cast<void>(seed_backfill(f));

    /* 2,000 one second observations hold a 15 minute span, and a minute. */
    f.reads.load_history(history_command(15 * 60 * second));
    f.reads.load_history(history_command(60 * second));

    CHECK(f.reads.history_reads_started() == 0);
    CHECK(f.reads.pending().empty());
}

TEST_CASE("a longer period starts one read of the depth it needs",
    "[reads][history]") {
    fixture f;
    f.open();
    const auto row = seed_backfill(f);

    f.reads.load_history(history_command(hour));

    CHECK(f.reads.history_reads_started() == 1);
    CHECK(f.reads.pending().contains(
        request_id::history(instance_id, account_id, "s-1")));
    CHECK(f.series.history(row).reading);

    /*
     * The answer names the range the read was asked for, whether the actor ran
     * it or refused it, because the answer is built from the request.
     */
    const auto answers = f.events.drain();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].kind == event::event_kind::series_history);
    CHECK(answers[0].history_from_ns == base + 1999 * second - hour);
}

TEST_CASE("a second period change while a read is in flight starts no other",
    "[reads][history]") {
    fixture f;
    f.open();
    static_cast<void>(seed_backfill(f));

    f.reads.load_history(history_command(hour));
    f.reads.load_history(history_command(0));
    f.reads.load_history(history_command(24 * hour));

    CHECK(f.reads.history_reads_started() == 1);
}

TEST_CASE("the answer is seeded and the same period is not asked again",
    "[reads][history]") {
    fixture f;
    f.open();
    const auto row = seed_backfill(f);
    const auto fine = domain::market_level::fine;
    REQUIRE(f.series.columns(row, fine).size == 2000);
    f.reads.load_history(history_command(hour));

    const auto from = base + 1999 * second - hour;
    REQUIRE(f.reads.apply(history_answer(from, 1500)));

    const auto held = f.series.columns(row, fine);
    CHECK(held.size == 3500);
    CHECK(held.at_ns[0] == base - 1500 * second);
    CHECK_FALSE(f.series.history(row).reading);
    CHECK(f.reads.pending().empty());

    f.reads.load_history(history_command(hour));
    CHECK(f.reads.history_reads_started() == 1);
}

TEST_CASE("an answer shorter than the period stops further reads",
    "[reads][history]") {
    fixture f;
    f.open();
    const auto row = seed_backfill(f);
    f.reads.load_history(history_command(24 * hour));
    REQUIRE(f.reads.history_reads_started() == 1);

    /* The deployment holds ten minutes more and nothing older. */
    const auto from = base + 1999 * second - 24 * hour;
    REQUIRE(f.reads.apply(history_answer(from, 600)));
    CHECK_FALSE(f.series.history(row).truncated);
    CHECK(f.series.history(row).floor_ns == from);

    for (int i = 0; i < 3; ++i)
        f.reads.load_history(history_command(24 * hour));
    CHECK(f.reads.history_reads_started() == 1);
}

TEST_CASE("a read that hit its bound is shown as such and is not repeated",
    "[reads][history]") {
    fixture f;
    f.open();
    const auto row = seed_backfill(f);
    f.reads.load_history(history_command(hour));

    REQUIRE(f.reads.apply(history_answer(base + 1999 * second - hour, 1800,
        true)));
    CHECK(f.series.history(row).truncated);

    f.reads.load_history(history_command(hour));
    CHECK(f.reads.history_reads_started() == 1);
}

TEST_CASE("a failed read is reported and retried by the next ask",
    "[reads][history]") {
    fixture f;
    f.open();
    const auto row = seed_backfill(f);
    f.reads.load_history(history_command(hour));

    auto failed = history_answer(base + 1999 * second - hour, 0);
    failed.message = "the instance did not answer";
    REQUIRE(f.reads.apply(failed));

    CHECK(f.series.failure(row) == "the instance did not answer");
    CHECK_FALSE(f.series.history(row).reading);
    CHECK(f.series.history(row).floor_ns == 0);
    CHECK(f.reads.pending().empty());
    CHECK(f.series.columns(row, domain::market_level::fine).size == 2000);

    f.reads.load_history(history_command(hour));
    CHECK(f.reads.history_reads_started() == 2);
}

TEST_CASE("a history answer for a series nobody holds is counted and dropped",
    "[reads][history]") {
    fixture f;
    f.open();
    REQUIRE(f.reads.apply(history_answer(base, 10)));
    CHECK(f.reads.unheld_points() == 1);
    CHECK(f.series.size() == 0);
}

TEST_CASE("a period for a series the table does not hold starts no read",
    "[reads][history]") {
    fixture f;
    f.open();
    f.reads.load_history(history_command(hour));
    CHECK(f.reads.history_reads_started() == 0);
}

TEST_CASE("a failed first read marks the series row as failed and ends the read",
    "[reads][backfill]") {
    fixture f;
    f.open();
    const auto row = f.series.intern(instance_id, account_id, "s-1");
    f.reads.start_market_backfill(instance_id, account_id, "s-1");
    REQUIRE_FALSE(f.reads.pending().empty());

    auto failed = answer_of(event::event_kind::series_points);
    failed.series_id = "s-1";
    failed.observed_at_ns = 2;
    failed.message = "the tick answer could not be decoded";
    REQUIRE(f.reads.apply(failed));

    CHECK(f.series.failure(row) == "the tick answer could not be decoded");
    CHECK(f.series.columns(row, domain::market_level::fine).size == 0);
    CHECK(f.reads.pending().empty());
}

/** @brief A first load answer of @p count ticks @p gap_ns apart from @p first_ns. */
[[nodiscard]] event::event first_load_answer(int count, std::int64_t gap_ns,
    std::int64_t first_ns = base) {
    auto fact = answer_of(event::event_kind::series_points);
    fact.series_id = "s-1";
    fact.observed_at_ns = 2;
    for (int i = 0; i < count; ++i)
        fact.points.push_back({.at_ns = first_ns + i * gap_ns, .value = 1.0 + i});
    return fact;
}

TEST_CASE("a first load of 200 ticks a second apart seeds 200 fine buckets",
    "[reads][backfill]") {
    fixture f;
    f.open();
    const auto row = f.series.intern(instance_id, account_id, "s-1");
    f.reads.start_market_backfill(instance_id, account_id, "s-1");

    REQUIRE(f.reads.apply(first_load_answer(200, second)));

    const auto fine = f.series.columns(row, domain::market_level::fine);
    CHECK(fine.size == 200);
    CHECK(fine.at_ns[0] == base);
    CHECK(fine.at_ns[199] == base + 199 * second);
    CHECK(f.series.failure(row).empty());
}

TEST_CASE("a live bucket that landed during the read survives it once",
    "[reads][backfill]") {
    fixture f;
    f.open();
    const auto row = f.series.intern(instance_id, account_id, "s-1");
    f.reads.start_market_backfill(instance_id, account_id, "s-1");
    const auto fine = domain::market_level::fine;

    /* The subscription is live first, so ticks close while the read is out. */
    const auto overlap = base + 10 * second;
    const auto after = base + 400 * second;
    REQUIRE(f.series.append(row, fine,
        {.at_ns = overlap, .value = 99.0, .open = 99.0, .high = 99.0,
            .low = 99.0}, 1) == state::append_result::applied);
    REQUIRE(f.series.append(row, fine,
        {.at_ns = after, .value = 98.0, .open = 98.0, .high = 98.0,
            .low = 98.0}, 1) == state::append_result::applied);

    REQUIRE(f.reads.apply(first_load_answer(300, second)));

    const auto held = f.series.columns(row, fine);
    CHECK(held.size == 301);
    std::size_t at_overlap = 0;
    for (std::size_t i = 0; i < held.size; ++i) {
        if (held.at_ns[i] == overlap) {
            ++at_overlap;
            CHECK(held.value[i] == 99.0);
        }
    }
    CHECK(at_overlap == 1);
    CHECK(held.at_ns[held.size - 1] == after);
    CHECK(held.value[held.size - 1] == 98.0);
}

namespace {

namespace worker = hedgr::runtime::worker;

[[nodiscard]] worker::read_request points_job() {
    return worker::market_points_request{.series_id = "s-1"};
}

[[nodiscard]] worker::read_request history_job() {
    return worker::market_history_request{.series_id = "s-1",
        .from_ns = 5,
        .to_ns = 9};
}

[[nodiscard]] event::event read_fact(int points, std::string message = {}) {
    auto fact = answer_of(event::event_kind::series_points);
    fact.series_id = "s-1";
    fact.message = std::move(message);
    for (int i = 0; i < points; ++i)
        fact.points.push_back({.at_ns = base + i * second, .value = 1.0});
    fact.tick_from_ns = 5;
    fact.tick_to_ns = 9;
    fact.tick_coordinate = "oresmd://fx/EURUSD";
    fact.tick_coordinates = 1;
    fact.ticks_read = static_cast<std::size_t>(points);
    return fact;
}

[[nodiscard]] bool names(const worker::market_read_report& report,
    std::string_view words) {
    return report.text.find(words) != std::string::npos;
}

}

TEST_CASE("a first load logs its series, span, count and coordinate at info",
    "[reads][log]") {
    auto fact = read_fact(45);
    fact.tick_to_ns = 0;
    const auto report = worker::report_market_read(points_job(), fact);

    CHECK_FALSE(report.warn);
    CHECK(names(report, "'s-1'"));
    CHECK(names(report, "first load"));
    CHECK(names(report, "from_ns=5 to_ns=now"));
    CHECK(names(report, "45 ticks of 'oresmd://fx/EURUSD'"));
    CHECK(names(report, "span read in full"));
}

TEST_CASE("a history read logs its series, range and count at info",
    "[reads][log]") {
    const auto report = worker::report_market_read(history_job(), read_fact(7));

    CHECK_FALSE(report.warn);
    CHECK(names(report, "'s-1'"));
    CHECK(names(report, "from_ns=5 to_ns=9"));
    CHECK(names(report, "7 ticks"));
}

TEST_CASE("a read at the ceiling or shortened says so, and several coordinates "
          "name the one plotted",
    "[reads][log]") {
    auto fact = read_fact(7);
    fact.history_truncated = true;
    fact.tick_coordinates = 3;
    const auto report = worker::report_market_read(history_job(), fact);

    CHECK_FALSE(report.warn);
    CHECK(names(report, "at the ceiling or a retry shortened the span"));
    CHECK(names(report, "plotted the first of 3 coordinates"));
    CHECK(names(report, "'oresmd://fx/EURUSD'"));
}

TEST_CASE("a failed read warns with the series, the request and the reason",
    "[reads][log]") {
    const auto points = worker::report_market_read(points_job(),
        read_fact(0, "the span holds more ticks than one reply carries "
                     "(range_too_large)"));
    CHECK(points.warn);
    CHECK(names(points, "'s-1'"));
    CHECK(names(points, "first load"));
    CHECK(names(points, "range_too_large"));

    const auto history = worker::report_market_read(history_job(),
        read_fact(0, "the tick answer could not be decoded"));
    CHECK(history.warn);
    CHECK(names(history, "'s-1'"));
    CHECK(names(history, "from_ns=5 to_ns=9"));
    CHECK(names(history, "the tick answer could not be decoded"));
}

TEST_CASE("an empty answer warns that the deployment returned nothing",
    "[reads][log]") {
    for (const auto& job : {points_job(), history_job()}) {
        const auto report = worker::report_market_read(job, read_fact(0));
        INFO(report.text);
        CHECK(report.warn);
        CHECK(names(report, "'s-1'"));
        CHECK(names(report, "no ticks"));
    }
}
