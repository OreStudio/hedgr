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
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/market_view.hpp"
#include "hedgr.ui/render/time_text.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

/*
 * Each test draws a whole window headless with ImGui's text log on, and reads
 * back what the window said: the foot word and the body sentence an operator
 * would read.
 */

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
using model::domain::window_kind;

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t now = 1'800'000'000 * second;

/** @brief The text one frame of a window drew. */
std::string drawn_text(hedgr::testing::desk& desk,
    model::state::window_handle handle, render::window_ui_state& state) {
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    input.wall_now_ns = now;
    const auto kind = desk.windows.kind(handle);
    std::string text;
    hedgr::testing::frame([&] {
        ImGui::LogToBuffer();
        render::renderer_for(kind)(handle, input, state, status, lock);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });
    return text;
}

[[nodiscard]] bool says(const std::string& text, std::string_view words) {
    return text.find(words) != std::string::npos;
}

}

TEST_CASE("a chart on a one-day period is live while its feed ticks",
    "[health][chart]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});

    /*
     * The minute level publishes a bucket only when the next one opens, so
     * its newest point trails the feed. The second level has a point from a
     * second ago.
     */
    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    desk.market.append(series, model::domain::market_level::medium,
        {.at_ns = now - 50 * second, .value = 1.25}, 1);
    desk.market.append(series, model::domain::market_level::fine,
        {.at_ns = now - second, .value = 1.26}, 2);

    render::window_ui_state state;
    state.market_view.chart_span = render::chart_span_index(24 * 3600 * second);
    REQUIRE(render::chart_span_ns(state.market_view.chart_span)
        == 24 * 3600 * second);

    const auto text = drawn_text(desk, handle, state);
    CHECK(says(text, "LIVE"));
    CHECK_FALSE(says(text, "STALE"));
    CHECK(says(text, render::as_of_text(now - second)));
}

/*
 * A chart is the exception: it says so in its plot area, with a glyph, and
 * plot_state_tests.cpp covers it.
 */
TEST_CASE("every other data window says the same thing when its session is lost",
    "[health][session]") {
    const std::vector<std::pair<window_kind, model::domain::window_state>>
        windows{
            {window_kind::term_structure,
                model::domain::term_structure_state{.label = "IR",
                    .curve_key = "c"}},
            {window_kind::crm_matrix,
                model::domain::crm_matrix_state{.crm_name = "majors"}},
            {window_kind::series_browser,
                model::domain::series_browser_state{}},
            {window_kind::command_centre,
                model::domain::command_centre_state{.instance_id = "i"}},
        };

    for (const auto& [kind, shape] : windows) {
        hedgr::testing::headless_imgui imgui;
        hedgr::testing::desk desk;
        desk.sessions.set_status(desk.session,
            model::domain::session_status::disconnected);
        const auto handle = desk.open(kind, shape);
        render::window_ui_state state;

        const auto text = drawn_text(desk, handle, state);
        INFO("window kind " << static_cast<int>(kind) << " drew: " << text);
        CHECK_FALSE(says(text, "not signed in"));
        CHECK(says(text, "NO DATA"));
    }
}

namespace {

/** @brief The text a CRM window draws over cells with these verdicts. */
std::string crm_text(const std::vector<std::pair<std::string,
    std::string>>& verdicts) {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    std::vector<model::domain::crm_cell> cells;
    for (const auto& [quote, status] : verdicts) {
        cells.push_back(model::domain::crm_cell{.crm_name = "majors",
            .base_currency_code = "GBP", .quote_currency_code = quote,
            .rate = 1.25, .status = status,
            .as_of = "2026-10-19T10:00:00Z"});
    }
    desk.crm.set("i", "a", "p", "majors", false, std::move(cells), {},
        now - 10 * second, 0);
    const auto handle = desk.open(window_kind::crm_matrix,
        model::domain::crm_matrix_state{.party_id = "p",
            .crm_name = "majors"});
    render::window_ui_state state;
    return drawn_text(desk, handle, state);
}

}

TEST_CASE("a CRM reads the deployment's verdicts in Hedgr's words",
    "[health][crm]") {
    const auto fresh = crm_text({{"USD", "fresh"}, {"EUR", "fresh"}});
    CHECK(says(fresh, "LIVE"));

    const auto lagging = crm_text({{"USD", "fresh"}, {"EUR", "stale"}});
    CHECK(says(lagging, "DELAYED"));
    CHECK_FALSE(says(lagging, "STALE"));

    const auto stopped = crm_text({{"USD", "fresh"}, {"EUR", "disconnected"}});
    CHECK(says(stopped, "STALE"));
}

TEST_CASE("a stale chart says how old its newest point is, capped at ten minutes", "[health][chart]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});

    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    desk.market.append(series, model::domain::market_level::fine,
        {.at_ns = now - 71 * 60 * second, .value = 1.26}, 1);

    render::window_ui_state state;
    const auto text = drawn_text(desk, handle, state);
    CHECK(says(text, "STALE"));
    CHECK(says(text, render::as_of_text(now - 71 * 60 * second)));
    CHECK(says(text, ">10m"));
}

TEST_CASE("the foot's age takes the colour of its verdict", "[health][chart]") {
    using model::domain::data_health;
    hedgr::testing::headless_imgui imgui;
    hedgr::ui::widget::foot_state foot;
    const std::int64_t at = now - 71 * 60 * second;

    render::set_health_foot(foot, data_health::stale, at, now, false);
    CHECK(foot.age == ">10m");
    CHECK(foot.as_of == render::as_of_text(at));
    CHECK(foot.colour == render::health_colour(data_health::stale));

    render::set_health_foot(foot, data_health::delayed, now - 20 * second, now,
        false);
    CHECK(foot.age == "20s");
    CHECK(foot.colour == render::health_colour(data_health::delayed));
    CHECK(foot.colour != render::health_colour(data_health::stale));

    render::set_health_foot(foot, data_health::live, now - 2 * second, now,
        false);
    CHECK(foot.age == "2s");
    CHECK(foot.colour == render::health_colour(data_health::live));

    render::set_health_foot(foot, data_health::unavailable, 0, now, false);
    CHECK(foot.age.empty());
}

TEST_CASE("a chart whose history read failed says so in its foot",
    "[health][chart][history]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});

    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    desk.market.append(series, model::domain::market_level::fine,
        {.at_ns = now - second, .value = 1.26}, 1);

    render::window_ui_state state;
    CHECK_FALSE(says(drawn_text(desk, handle, state), "ERROR"));

    desk.market.fail(series, "the observation answer could not be decoded", 2);
    const auto text = drawn_text(desk, handle, state);
    CHECK(says(text, "ERROR"));
    CHECK_FALSE(says(text, "the observation answer could not be decoded"));

    hedgr::ui::widget::foot_state foot;
    render::set_history_unavailable_foot(foot,
        "the observation answer could not be decoded");
    CHECK(foot.detail == "the observation answer could not be decoded");
}

TEST_CASE("the history note takes the failure colour and only on a failure",
    "[health][chart][history]") {
    using model::domain::data_health;
    hedgr::testing::headless_imgui imgui;
    hedgr::ui::widget::foot_state foot;
    render::set_health_foot(foot, data_health::live, now - second, now, false);
    const auto live = foot.word;

    render::set_history_unavailable_foot(foot, "");
    CHECK(foot.word == live);

    render::set_history_unavailable_foot(foot, "refused");
    CHECK(foot.word == "ERROR");
    CHECK(foot.colour == hedgr::ui::theme::failure_colour);
}
