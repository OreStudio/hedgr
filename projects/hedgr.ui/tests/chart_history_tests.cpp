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
#include "hedgr.ui/render/market_view.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/*
 * A chart says which span it plots and the runtime decides what to read, so
 * these tests draw whole frames headless and read the commands the chart
 * pushed and the words it drew.
 */

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
using model::domain::window_kind;

constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t hour = 3600 * second;
constexpr std::int64_t now = 1'800'000'000 * second;

struct chart final {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    model::state::window_handle handle;
    model::state::series_handle series;
    render::window_ui_state state;

    chart() {
        handle = desk.open(window_kind::chart,
            model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});
        series = desk.market.intern("i", "a", "s");
        desk.windows.set_series(handle, std::span{&series, 1});
        state.market_view.chart_span = render::chart_span_index(hour);
    }

    /** @brief The first backfill: 2,000 one second observations, 33 minutes. */
    void backfill() {
        std::vector<model::domain::market_point> points;
        for (int i = 0; i < 2000; ++i)
            points.push_back({.at_ns = now - (1999 - i) * second, .value = 1.0 + i});
        desk.market.seed(series, points, 1);
    }

    std::string frame() {
        hedgr::ui::widget::status_bar status;
        render::lock_control lock;
        auto input = desk.input();
        input.wall_now_ns = now;
        std::string text;
        hedgr::testing::frame([&] {
            ImGui::LogToBuffer();
            render::renderer_for(window_kind::chart)(handle, input, state,
                status, lock);
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
        });
        return text;
    }

    /** @brief The history commands pushed since the last call. */
    std::vector<model::command::command> asked() {
        std::vector<model::command::command> r;
        for (auto& c : desk.commands.drain()) {
            if (c.kind == model::command::command_kind::load_history)
                r.push_back(std::move(c));
        }
        return r;
    }
};

[[nodiscard]] bool says(const std::string& text, std::string_view words) {
    return text.find(words) != std::string::npos;
}

}

TEST_CASE("a chart asks for its span once and again only when the span changes",
    "[chart][history]") {
    chart c;
    c.backfill();

    static_cast<void>(c.frame());
    const auto first = c.asked();
    REQUIRE(first.size() == 1);
    CHECK(first[0].instance_id == "i");
    CHECK(first[0].account_id == "a");
    CHECK(first[0].series_id == "s");
    CHECK(first[0].history_span_ns == hour);

    for (int i = 0; i < 3; ++i)
        static_cast<void>(c.frame());
    CHECK(c.asked().empty());

    c.state.market_view.chart_span = render::chart_span_index(0);
    static_cast<void>(c.frame());
    const auto all = c.asked();
    REQUIRE(all.size() == 1);
    CHECK(all[0].history_span_ns == 0);
}

TEST_CASE("a chart whose first backfill is still in flight asks for nothing",
    "[chart][history]") {
    chart c;
    static_cast<void>(c.frame());
    CHECK(c.asked().empty());

    c.backfill();
    static_cast<void>(c.frame());
    CHECK(c.asked().size() == 1);
}

TEST_CASE("a chart says a deeper read is in flight and keeps its plot",
    "[chart][history]") {
    chart c;
    c.backfill();
    static_cast<void>(c.frame());
    c.desk.market.begin_history(c.series);

    const auto text = c.frame();

    CHECK(says(text, "Reading older history..."));
    CHECK(says(text, "Points 2000"));
}

TEST_CASE("a chart asks again after the read that was in flight ends cleanly",
    "[chart][history]") {
    chart c;
    c.backfill();
    static_cast<void>(c.frame());
    REQUIRE(c.asked().size() == 1);

    c.desk.market.begin_history(c.series);
    static_cast<void>(c.frame());
    c.state.market_view.chart_span = render::chart_span_index(0);
    static_cast<void>(c.frame());
    CHECK(c.asked().size() == 1);

    c.desk.market.finish_history(c.series, now - hour, false);
    static_cast<void>(c.frame());
    const auto again = c.asked();
    REQUIRE(again.size() == 1);
    CHECK(again[0].history_span_ns == 0);
}

TEST_CASE("a chart does not ask again after a read that failed",
    "[chart][history]") {
    chart c;
    c.backfill();
    static_cast<void>(c.frame());
    REQUIRE(c.asked().size() == 1);

    c.desk.market.begin_history(c.series);
    static_cast<void>(c.frame());
    c.desk.market.abandon_history(c.series);
    c.desk.market.fail(c.series, "the instance did not answer", 2);
    const auto text = c.frame();

    CHECK(says(text, "the instance did not answer"));
    CHECK(c.asked().empty());
    static_cast<void>(c.frame());
    CHECK(c.asked().empty());

    c.state.market_view.chart_span = render::chart_span_index(24 * hour);
    static_cast<void>(c.frame());
    CHECK(c.asked().size() == 1);
}

TEST_CASE("a seeded answer is plotted over the longer span by the next frame",
    "[chart][history]") {
    chart c;
    c.backfill();
    CHECK(says(c.frame(), "Points 2000"));

    std::vector<model::domain::market_point> older;
    for (int i = 1500; i > 0; --i)
        older.push_back({.at_ns = now - 1999 * second - i * second, .value = 0.5});
    c.desk.market.seed(c.series, older, 2);
    c.desk.market.finish_history(c.series, now - hour, false);

    const auto text = c.frame();
    CHECK(says(text, "Points 3500"));
    CHECK(says(text, "shows 58m of 1h"));
}

TEST_CASE("a chart says when the server held less than the period",
    "[chart][history]") {
    chart c;
    c.backfill();
    c.desk.market.finish_history(c.series, now - hour, false);

    const auto text = c.frame();
    CHECK(says(text, "shows 33m of 1h"));

    /* A span the series covers says nothing extra. */
    c.state.market_view.chart_span = render::chart_span_index(15 * 60 * second);
    CHECK_FALSE(says(c.frame(), "shows"));
}
