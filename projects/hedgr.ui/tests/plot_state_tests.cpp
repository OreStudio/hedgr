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
#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/plot_state.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/*
 * The state of a plot area has no text the ImGui log can be trusted with for
 * its glyph, so each test reads the vertices the frame submitted, by colour,
 * and the log for the captions that are ordinary text.
 */

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using model::domain::session_status;
using widget::plot_state;

/** @brief What one frame logged as text and submitted as vertices. */
struct drawn final {
    std::string text;
    std::vector<ImDrawVert> vertices;

    [[nodiscard]] std::vector<ImVec2> of(ImU32 colour) const {
        std::vector<ImVec2> at;
        for (const auto& v : vertices)
            if (v.col == colour)
                at.push_back(v.pos);
        return at;
    }

    [[nodiscard]] bool says(std::string_view words) const {
        return text.find(words) != std::string::npos;
    }
};

template<class Body>
drawn capture(Body body) {
    drawn result;
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    body();
    result.text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    const ImDrawData* data = ImGui::GetDrawData();
    for (const ImDrawList* list : data->CmdLists)
        result.vertices.insert(result.vertices.end(), list->VtxBuffer.begin(),
            list->VtxBuffer.end());
    return result;
}

[[nodiscard]] ImVec2 centre_of(const std::vector<ImVec2>& at) {
    ImVec2 lo(1e9f, 1e9f);
    ImVec2 hi(-1e9f, -1e9f);
    for (const auto p : at) {
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
    return ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
}

constexpr ImVec2 area_min(100.0f, 50.0f);
constexpr ImVec2 area_max(500.0f, 350.0f);
constexpr ImVec2 area_centre(300.0f, 200.0f);

/** @brief The widget drawn into a fixed window over the area above. */
drawn widget_frame(plot_state state, bool captioned,
    std::string_view reason = {}) {
    return capture([&] {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        widget::draw_plot_state(state, area_min, area_max,
            theme::plot_state_glyph_size, captioned, reason);
        ImGui::End();
    });
}

/** @brief One frame of a window's renderer over @p desk. */
drawn window_frame(hedgr::testing::desk& desk,
    model::state::window_handle handle, render::window_ui_state& state) {
    widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    input.wall_now_ns = 1'800'000'000'000'000'000;
    const auto kind = desk.windows.kind(handle);
    return capture([&] {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(kind == model::domain::window_kind::mini_plot
            ? ImVec2(240.0f, 180.0f) : ImVec2(1000.0f, 700.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("window", nullptr, ImGuiWindowFlags_NoDecoration);
        render::renderer_for(kind)(handle, input, state, status, lock);
        ImGui::End();
        ImGui::PopStyleVar();
    });
}

[[nodiscard]] model::state::window_handle open_window(
    hedgr::testing::desk& desk, model::domain::window_kind kind) {
    if (kind == model::domain::window_kind::chart)
        return desk.open(kind, model::domain::chart_state{
            .label = "FX GBP/USD", .series_id = "s"});
    return desk.open(kind, model::domain::mini_plot_state{
        .label = "FX GBP/USD", .series_id = "s"});
}

/** @brief Holds a series row for @p handle that nothing has read yet. */
void hold_unread_series(hedgr::testing::desk& desk,
    model::state::window_handle handle) {
    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
}

/** @brief The three colours that mean "a plot state was drawn". */
struct marks final {
    std::size_t disconnected;
    std::size_t failed;
    std::size_t spinner;
};

[[nodiscard]] marks marks_of(const drawn& frame) {
    return {frame.of(theme::plot_disconnected_colour).size(),
        frame.of(theme::plot_failed_colour).size(),
        frame.of(theme::plot_spinner_colour).size()};
}

}

TEST_CASE("a plot state follows the session, then whether the series is read",
    "[plot_state]") {
    for (const bool reading : {false, true}) {
        CHECK(widget::plot_state_for(session_status::disconnected, reading)
            == plot_state::disconnected);
        CHECK(widget::plot_state_for(session_status::failed, reading)
            == plot_state::failed);
        CHECK(widget::plot_state_for(session_status::connecting, reading)
            == plot_state::reading);
    }
    CHECK(widget::plot_state_for(session_status::authenticated, true)
        == plot_state::reading);
    CHECK(widget::plot_state_for(session_status::authenticated, false)
        == plot_state::none);
}

TEST_CASE("the disconnected glyph is dimmer than the failed one",
    "[plot_state]") {
    const auto luma = [](ImU32 c) {
        return (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
    };
    CHECK(luma(theme::plot_disconnected_colour)
        < luma(theme::plot_failed_colour));
}

TEST_CASE("a plot state draws its own glyph and no other", "[plot_state]") {
    hedgr::testing::headless_imgui imgui;

    const auto off = marks_of(widget_frame(plot_state::disconnected, false));
    CHECK(off.disconnected > 0);
    CHECK(off.failed == 0);
    CHECK(off.spinner == 0);

    const auto failed = marks_of(widget_frame(plot_state::failed, false));
    CHECK(failed.disconnected == 0);
    CHECK(failed.failed > 0);
    CHECK(failed.spinner == 0);

    const auto history
        = marks_of(widget_frame(plot_state::history_unavailable, false));
    CHECK(history.failed > 0);
    CHECK(history.disconnected == 0);
    CHECK(history.spinner == 0);

    const auto reading = marks_of(widget_frame(plot_state::reading, false));
    CHECK(reading.disconnected == 0);
    CHECK(reading.failed == 0);
    CHECK(reading.spinner > 0);

    const auto none = widget_frame(plot_state::none, true);
    const auto nothing = marks_of(none);
    CHECK(nothing.disconnected + nothing.failed + nothing.spinner == 0);
    CHECK_FALSE(none.says("Not connected"));
    CHECK_FALSE(none.says("Reading the series"));
}

TEST_CASE("the glyph and the spinner sit in the middle of the plot area",
    "[plot_state]") {
    hedgr::testing::headless_imgui imgui;

    const auto plug = widget_frame(plot_state::disconnected, false)
        .of(theme::plot_disconnected_colour);
    const ImVec2 plug_at = centre_of(plug);
    CHECK(std::abs(plug_at.x - area_centre.x) < 8.0f);
    CHECK(std::abs(plug_at.y - area_centre.y) < 8.0f);

    const auto ring = widget_frame(plot_state::reading, false)
        .of(theme::plot_spinner_colour);
    const float reach = theme::plot_state_glyph_size * 0.5f
        + theme::plot_spinner_weight;
    for (const auto p : ring) {
        CHECK(std::hypot(p.x - area_centre.x, p.y - area_centre.y) <= reach);
    }
}

TEST_CASE("the spinner turns with the clock", "[plot_state]") {
    hedgr::testing::headless_imgui imgui;
    const auto first = widget_frame(plot_state::reading, false)
        .of(theme::plot_spinner_colour);
    for (int i = 0; i < 10; ++i)
        static_cast<void>(widget_frame(plot_state::reading, false));
    const auto later = widget_frame(plot_state::reading, false)
        .of(theme::plot_spinner_colour);

    REQUIRE(first.size() == later.size());
    bool moved = false;
    for (std::size_t i = 0; i < first.size(); ++i)
        moved = moved || std::abs(first[i].x - later[i].x) > 0.5f
            || std::abs(first[i].y - later[i].y) > 0.5f;
    CHECK(moved);

    /* A glyph is still: the same plug is drawn in the same place. */
    const auto plug_a = widget_frame(plot_state::failed, false)
        .of(theme::plot_failed_colour);
    const auto plug_b = widget_frame(plot_state::failed, false)
        .of(theme::plot_failed_colour);
    REQUIRE(plug_a.size() == plug_b.size());
    for (std::size_t i = 0; i < plug_a.size(); ++i) {
        CHECK(plug_a[i].x == plug_b[i].x);
        CHECK(plug_a[i].y == plug_b[i].y);
    }
}

TEST_CASE("a captioned state says its words and an uncaptioned one says none",
    "[plot_state]") {
    hedgr::testing::headless_imgui imgui;

    CHECK(widget_frame(plot_state::disconnected, true).says("Not connected"));
    CHECK(widget_frame(plot_state::reading, true).says("Reading the series..."));
    CHECK_FALSE(widget_frame(plot_state::reading, true).says("Not connected"));

    const auto history = widget_frame(plot_state::history_unavailable, true,
        "no answer");
    CHECK(history.says("History unavailable"));
    CHECK(history.says("no answer"));
    CHECK_FALSE(history.says("Not connected"));

    const auto failed = widget_frame(plot_state::failed, true, "bad password");
    CHECK(failed.says("Not connected"));
    CHECK(failed.says("bad password"));

    for (const auto state : {plot_state::disconnected, plot_state::failed,
             plot_state::reading}) {
        const auto bare = widget_frame(state, false, "bad password");
        CHECK_FALSE(bare.says("Not connected"));
        CHECK_FALSE(bare.says("Reading"));
        CHECK_FALSE(bare.says("bad password"));
    }
}

TEST_CASE("a chart with no open session says it is not connected, not reading",
    "[plot_state][chart]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = open_window(desk, model::domain::window_kind::chart);
    hold_unread_series(desk, handle);
    render::window_ui_state state;

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(off.says("Not connected"));
    CHECK(off.says("NO DATA"));
    CHECK_FALSE(off.says("Reading the series"));
    CHECK(marks_of(off).disconnected > 0);
    CHECK(marks_of(off).spinner == 0);

    desk.sessions.set_status(desk.session, session_status::failed);
    desk.sessions.set_last_error(desk.session, "certificate rejected");
    const auto failed = window_frame(desk, handle, state);
    CHECK(failed.says("Not connected"));
    CHECK(failed.says("certificate rejected"));
    CHECK_FALSE(failed.says("Reading the series"));
    CHECK(marks_of(failed).failed > 0);
    CHECK(marks_of(failed).disconnected == 0);
}

TEST_CASE("a chart whose session is connecting spins and says it is reading",
    "[plot_state][chart]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = open_window(desk, model::domain::window_kind::chart);
    hold_unread_series(desk, handle);
    render::window_ui_state state;

    desk.sessions.set_status(desk.session, session_status::connecting);
    const auto connecting = window_frame(desk, handle, state);
    CHECK(connecting.says("Reading the series..."));
    CHECK_FALSE(connecting.says("Not connected"));
    CHECK(marks_of(connecting).spinner > 0);
    CHECK(marks_of(connecting).disconnected + marks_of(connecting).failed == 0);
}

TEST_CASE("a connected chart still reading its series spins, and one whose"
          " read failed says history is unavailable",
    "[plot_state][chart]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = open_window(desk, model::domain::window_kind::chart);
    hold_unread_series(desk, handle);
    render::window_ui_state state;

    const auto reading = window_frame(desk, handle, state);
    CHECK(reading.says("Reading the series..."));
    CHECK(marks_of(reading).spinner > 0);
    CHECK_FALSE(reading.says("Not connected"));

    const auto series = desk.windows.series(handle).front();
    desk.market.fail(series, "no such series", 1);
    const auto empty = window_frame(desk, handle, state);
    CHECK(empty.says("History unavailable"));
    CHECK(empty.says("no such series"));
    CHECK_FALSE(empty.says("Reading the series"));
    CHECK_FALSE(empty.says("No observations for this series yet."));
    CHECK(marks_of(empty).spinner == 0);
    CHECK(marks_of(empty).failed > 0);
    CHECK(marks_of(empty).disconnected == 0);
}

TEST_CASE("a card shows the glyph alone for each state, under its header",
    "[plot_state][mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = open_window(desk, model::domain::window_kind::mini_plot);
    hold_unread_series(desk, handle);
    render::window_ui_state state;

    const auto expect_bare = [](const drawn& card) {
        CHECK(card.says("FX GBP/USD"));
        CHECK_FALSE(card.says("Not connected"));
        CHECK_FALSE(card.says("Reading"));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    expect_bare(off);
    CHECK(marks_of(off).disconnected > 0);
    CHECK(marks_of(off).spinner == 0);

    desk.sessions.set_status(desk.session, session_status::failed);
    desk.sessions.set_last_error(desk.session, "certificate rejected");
    const auto failed = window_frame(desk, handle, state);
    expect_bare(failed);
    CHECK_FALSE(failed.says("certificate rejected"));
    CHECK(marks_of(failed).failed > 0);
    CHECK(marks_of(failed).disconnected == 0);

    desk.sessions.set_status(desk.session, session_status::connecting);
    const auto connecting = window_frame(desk, handle, state);
    expect_bare(connecting);
    CHECK(marks_of(connecting).spinner > 0);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    const auto reading = window_frame(desk, handle, state);
    expect_bare(reading);
    CHECK(marks_of(reading).spinner > 0);
    CHECK(marks_of(reading).disconnected + marks_of(reading).failed == 0);
}

TEST_CASE("a card's glyph is centred below its header, inside the card",
    "[plot_state][mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = open_window(desk, model::domain::window_kind::mini_plot);
    hold_unread_series(desk, handle);
    render::window_ui_state state;
    desk.sessions.set_status(desk.session, session_status::disconnected);

    const auto plug = window_frame(desk, handle, state)
        .of(theme::plot_disconnected_colour);
    REQUIRE_FALSE(plug.empty());
    const ImVec2 at = centre_of(plug);
    CHECK(std::abs(at.x - 120.0f) < 8.0f);
    CHECK(at.y > 90.0f);
    CHECK(at.y < 170.0f);
}
