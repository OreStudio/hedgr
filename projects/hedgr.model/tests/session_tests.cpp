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
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/domain/window_json_io.hpp"
#include "hedgr.model/event/event.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <variant>

namespace domain = hedgr::model::domain;

TEST_CASE("a session's mode follows its tenant", "[session]") {
    /*
     * The deployment's own rule, stated once here so the interface never has to
     * hold it: the system tenant's session acts on the deployment, and every
     * other tenant's acts inside itself.
     */
    REQUIRE(domain::session_mode_for(domain::system_tenant_id)
        == domain::session_mode::system_administration);
    REQUIRE(domain::session_mode_for("590a42e4-c418-4cf2-8fa2-22ee632ada3d")
        == domain::session_mode::application);
    REQUIRE(domain::session_mode_for("") == domain::session_mode::application);

    REQUIRE(domain::to_string(domain::session_mode::system_administration)
        == "system_administration");
    REQUIRE(domain::to_string(domain::session_mode::application)
        == "application");
}

TEST_CASE("a command centre state survives the stored form", "[window]") {
    /*
     * The window wall is restored from the store, so the state has to make the
     * round trip. The instance is what a restored window names when there is no
     * live session to ask, which is why it is persisted at all.
     */
    const domain::window_state state =
        domain::command_centre_state{"inst-clever_dijkstra"};

    const auto encoded =
        domain::window_state_to_json(domain::window_kind::command_centre, state);
    REQUIRE(encoded.has_value());

    const auto decoded = domain::window_state_from_json(
        domain::window_kind::command_centre, *encoded);
    REQUIRE(decoded.has_value());

    const auto* centre = std::get_if<domain::command_centre_state>(&*decoded);
    REQUIRE(centre != nullptr);
    REQUIRE(centre->instance_id == "inst-clever_dijkstra");
}

TEST_CASE("a saved window state missing fields reads with their defaults",
    "[window]") {
    for (auto value = 0; value < static_cast<int>(domain::window_kind_count);
         ++value) {
        const auto kind = static_cast<domain::window_kind>(value);
        INFO("kind " << domain::to_string(kind));
        REQUIRE(domain::window_state_from_json(kind, "{}").has_value());
    }

    const auto chart = domain::window_state_from_json(
        domain::window_kind::chart, R"({"series_id":"trades"})");
    REQUIRE(chart.has_value());
    const auto* state = std::get_if<domain::chart_state>(&*chart);
    REQUIRE(state != nullptr);
    REQUIRE(state->series_id == "trades");
    REQUIRE(state->max_points == domain::chart_state{}.max_points);
}

TEST_CASE("a chart's own view survives the stored form", "[window]") {
    constexpr std::int64_t day = std::int64_t{24} * 3600 * 1'000'000'000;
    const domain::window_state state = domain::chart_state{
        .label = "FX GBP/USD", .series_id = "s-1",
        .view = domain::chart_view{.span_ns = day,
            .style = domain::chart_style::candles, .ema = true}};

    const auto encoded =
        domain::window_state_to_json(domain::window_kind::chart, state);
    REQUIRE(encoded.has_value());
    CHECK(encoded->find("\"candles\"") != std::string::npos);

    const auto decoded =
        domain::window_state_from_json(domain::window_kind::chart, *encoded);
    REQUIRE(decoded.has_value());
    const auto* chart = std::get_if<domain::chart_state>(&*decoded);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->view.has_value());
    CHECK(chart->view->span_ns == day);
    CHECK(chart->view->style == domain::chart_style::candles);
    CHECK(chart->view->ema);
}

TEST_CASE("a chart saved before charts kept a view reads with none",
    "[window]") {
    const auto decoded = domain::window_state_from_json(
        domain::window_kind::chart,
        R"({"label":"FX GBP/USD","series_id":"s-1","max_points":600})");
    REQUIRE(decoded.has_value());
    const auto* chart = std::get_if<domain::chart_state>(&*decoded);
    REQUIRE(chart != nullptr);
    CHECK(chart->series_id == "s-1");
    CHECK_FALSE(chart->view.has_value());
}

TEST_CASE("every window kind's text parses back to the kind", "[window]") {
    for (auto value = 0; value < static_cast<int>(domain::window_kind_count);
         ++value) {
        const auto kind = static_cast<domain::window_kind>(value);
        INFO("kind " << value);
        const auto parsed =
            domain::window_kind_from_string(domain::to_string(kind));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == kind);
    }
    CHECK(domain::window_kind_count == 13);
    CHECK(domain::to_string(domain::window_kind::mini_plot) == "mini_plot");
    CHECK(domain::to_string(domain::window_kind::vol_surface) == "vol_surface");
    CHECK(domain::to_string(domain::window_kind::rate_ladder) == "rate_ladder");
}

TEST_CASE("a mini plot's state survives the stored form", "[window]") {
    const domain::window_state state =
        domain::mini_plot_state{.label = "FX GBP/USD", .series_id = "s-1",
            .span_ns = std::int64_t{15} * 60 * 1'000'000'000};
    const auto encoded =
        domain::window_state_to_json(domain::window_kind::mini_plot, state);
    REQUIRE(encoded.has_value());
    const auto decoded = domain::window_state_from_json(
        domain::window_kind::mini_plot, *encoded);
    REQUIRE(decoded.has_value());
    const auto* card = std::get_if<domain::mini_plot_state>(&*decoded);
    REQUIRE(card != nullptr);
    CHECK(card->label == "FX GBP/USD");
    CHECK(card->series_id == "s-1");
    CHECK(card->span_ns == std::int64_t{15} * 60 * 1'000'000'000);

    /* All is zero, which is not the default, so it has to survive too. */
    const domain::window_state all =
        domain::mini_plot_state{.label = "x", .series_id = "s", .span_ns = 0};
    const auto all_back = domain::window_state_from_json(
        domain::window_kind::mini_plot,
        *domain::window_state_to_json(domain::window_kind::mini_plot, all));
    REQUIRE(all_back.has_value());
    CHECK(std::get<domain::mini_plot_state>(*all_back).span_ns == 0);

    /* A card saved before it had a span reads as the chart default. */
    const auto old = domain::window_state_from_json(
        domain::window_kind::mini_plot,
        R"({"label":"FX GBP/USD","series_id":"s-1"})");
    REQUIRE(old.has_value());
    CHECK(std::get<domain::mini_plot_state>(*old).span_ns
        == std::int64_t{5} * 60 * 1'000'000'000);
}

TEST_CASE("the parties a sign-in lists are read from its columns", "[event]") {
    using hedgr::model::event::holds_party;
    using hedgr::model::event::party_count;

    /*
     * The shape IAM's answer is serialised in: id, unit separator, name, and a
     * record separator between parties. Both readers of it agree here rather
     * than in each component, so a separator changed in one place cannot leave
     * the other reading names as ids.
     */
    const std::string parties = "p-system\x1f" "System Party\x1e"
                                "p-uk\x1f" "Acme UK plc\x1e"
                                "p-hk\x1f" "Acme HK Ltd";

    CHECK(party_count(parties) == 3);
    CHECK(holds_party(parties, "p-uk"));
    CHECK(holds_party(parties, "p-hk"));
    CHECK(holds_party(parties, "p-system"));
    CHECK_FALSE(holds_party(parties, "p-gone"));

    /*
     * A name is not an id: the second column must never answer for the first,
     * or a party remembered by name would be asked for as an id.
     */
    CHECK_FALSE(holds_party(parties, "Acme UK plc"));
    CHECK_FALSE(holds_party(parties, "System Party"));

    /* Nothing chosen and nothing listed are different from zero parties. */
    CHECK(party_count("") == 0);
    CHECK(party_count("p-only\x1f" "Only party") == 1);
    CHECK_FALSE(holds_party(parties, ""));
    CHECK_FALSE(holds_party("", "p-uk"));
}
