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
#include "headless_desk.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/party_prompt.hpp"
#include "hedgr.ui/render/windows.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

using namespace hedgr::testing;

TEST_CASE("a foot selection asks for a different party only", "[party][foot]") {
    render::party_list parties;
    REQUIRE(render::update_party_list(parties,
        "p1\x1f" "First party\x1ep2\x1f" "Second party", "p1",
        "First party"));
    REQUIRE(parties.size() == 2);

    CHECK_FALSE(render::acted_party(parties, 0, "p1").has_value());
    CHECK(render::acted_party(parties, 1, "p1") == "p2");
    CHECK_FALSE(render::acted_party(parties, 2, "p1").has_value());
    CHECK_FALSE(render::acted_party(parties, -1, "p1").has_value());
}

TEST_CASE("a window's foot learns the session's parties", "[party][foot]") {
    headless_imgui imgui;
    desk desk;
    desk.sessions.set_parties(desk.session,
        "p1\x1f" "First party\x1ep2\x1f" "Second party");
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{}, "b");

    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    frame([&] {
        render::render_series_browser(handle, input, state, status, lock);
    });

    /* The control's list is the session's, and it starts on the held party. */
    CHECK(state.parties.size() == 2);
    CHECK(state.parties.names.size() == 2);
    CHECK(state.acting_party == 0);
}

TEST_CASE("a sign-in with parties to choose from is asked which one",
    "[party][prompt]") {
    headless_imgui imgui;
    static_cast<void>(imgui);
    desk desk;
    const auto session = desk.session;

    /*
     * The account row the session reads is the one its account id names, and
     * the desk's session is bound to "a". It starts with no party remembered,
     * which is the state of an account that has never chosen one.
     */
    static_cast<void>(desk.accounts.upsert(model::domain::account{
        .id = "a", .instance_id = "i", .name = "Desk",
        .principal = "trader@desk"}));
    desk.sessions.set_parties(session,
        "p1\x1f" "First party\x1e" "p2\x1f" "Second party");
    auto input = desk.input();
    CHECK(render::needs_party_choice(input, session));

    /*
     * A single-party login is never asked: the deployment's answer is the only
     * answer, so the question would have one choice.
     */
    desk.sessions.set_parties(session, "p1\x1f" "Only party");
    CHECK_FALSE(render::needs_party_choice(input, session));

    /*
     * Once a party is remembered the question is answered for good, and the
     * runtime moves the session to that party at the sign-in instead.
     */
    desk.sessions.set_parties(session,
        "p1\x1f" "First party\x1e" "p2\x1f" "Second party");
    const auto handle = desk.accounts.find("a");
    REQUIRE(handle.has_value());
    auto remembered = *desk.accounts.get(*handle);
    remembered.party_id = "p2";
    static_cast<void>(desk.accounts.upsert(remembered));
    CHECK_FALSE(render::needs_party_choice(input, session));

    /* A session that has not signed in is not asked either. */
    desk.sessions.set_status(session,
        model::domain::session_status::connecting);
    CHECK_FALSE(render::needs_party_choice(input, session));
}

TEST_CASE("the party prompt opens for a session that has to choose",
    "[party][prompt]") {
    headless_imgui imgui;
    desk desk;
    static_cast<void>(desk.accounts.upsert(model::domain::account{
        .id = "a", .instance_id = "i", .name = "Desk",
        .principal = "trader@desk"}));
    desk.sessions.set_parties(desk.session,
        "p1\x1f" "Acme UK plc\x1e" "p2\x1f" "Acme US Inc");
    auto input = desk.input();

    render::party_prompt_state state;
    frame([&] { render::draw_party_prompt(input, state); });

    /* The question is up, listing the parties, on the one that is acting. */
    CHECK(state.account_id == "a");
    CHECK(state.parties.size() == 2);
    CHECK(std::string{state.parties.name(
              static_cast<std::size_t>(state.choice))} == "Acme UK plc");

    /* A session that has declined is not asked again in this run. */
    render::party_prompt_state declined;
    declined.dismissed.insert(std::string{"i\x1f"} + "a");
    frame([&] { render::draw_party_prompt(input, declined); });
    CHECK(declined.instance_id.empty());
    CHECK(declined.parties.empty());

    /*
     * And an account that remembers a party is not asked at all: the runtime
     * moves the session there at the sign-in.
     */
    const auto handle = desk.accounts.find("a");
    REQUIRE(handle.has_value());
    auto remembered = *desk.accounts.get(*handle);
    remembered.party_id = "p2";
    static_cast<void>(desk.accounts.upsert(remembered));
    auto answered_input = desk.input();
    render::party_prompt_state answered;
    frame([&] { render::draw_party_prompt(answered_input, answered); });
    CHECK(answered.instance_id.empty());
}

TEST_CASE("the party prompt keeps the row the operator moved it to",
    "[party][prompt]") {
    /*
     * The combo used to be re-seeded from the session's own party on every
     * frame, so a selection snapped back to the deployment's default before it
     * could be confirmed. The choice is seeded once, when the prompt opens, and
     * then belongs to the operator.
     */
    headless_imgui imgui;
    desk desk;
    static_cast<void>(desk.accounts.upsert(model::domain::account{
        .id = "a", .instance_id = "i", .name = "Desk",
        .principal = "trader@desk"}));
    desk.sessions.set_parties(desk.session,
        "p1\x1f" "Acme UK plc\x1e" "p2\x1f" "Acme US Inc");
    desk.sessions.set_account_result(desk.session, "Acme", "Acme UK plc",
        "p1", model::domain::secret_string{"t"});
    auto input = desk.input();

    render::party_prompt_state state;
    frame([&] { render::draw_party_prompt(input, state); });
    /* It opens on the party the session acts as. */
    CHECK(state.choice == 0);

    /* The operator moves it to the second row. */
    state.choice = 1;
    frame([&] { render::draw_party_prompt(input, state); });
    CHECK(state.choice == 1);

    /* And it stays there however many frames pass. */
    frame([&] { render::draw_party_prompt(input, state); });
    frame([&] { render::draw_party_prompt(input, state); });
    CHECK(state.choice == 1);
}
