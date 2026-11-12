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
#include "hedgr.model/domain/deployment.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "software_raster.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/*
 * The meaning of each mark is the UX language, so these tests read the
 * vertices a frame submitted by colour and by shape, and the log for the words
 * that are ordinary text. A window is judged by the mark it draws and by the
 * sentence it no longer writes.
 */

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using model::domain::session_status;
using model::domain::window_kind;
using widget::view_facts;
using widget::view_state;
using widget::view_verdict;
using widget::view_words;

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

[[nodiscard]] ImVec2 extent_of(const std::vector<ImVec2>& at) {
    ImVec2 lo(1e9f, 1e9f);
    ImVec2 hi(-1e9f, -1e9f);
    for (const auto p : at) {
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
    return ImVec2(hi.x - lo.x, hi.y - lo.y);
}

constexpr ImVec2 area_min(100.0f, 50.0f);
constexpr ImVec2 area_max(500.0f, 350.0f);
constexpr ImVec2 area_centre(300.0f, 200.0f);

constexpr view_verdict not_connected_verdict{
    view_state::not_connected, view_words::not_connected, {}};
constexpr view_verdict error_verdict{
    view_state::error, view_words::connection_failed, "bad password"};
constexpr view_verdict no_data_verdict{
    view_state::no_data, view_words::empty, "This series holds no points."};
constexpr view_verdict reading_verdict{
    view_state::reading, view_words::none, {}};

/** @brief The widget drawn into a fixed window over the area above. */
drawn widget_frame(const view_verdict& verdict) {
    return capture([&] {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        widget::draw_view_state(verdict, area_min, area_max,
            theme::view_state_glyph_size);
        ImGui::End();
    });
}

/** @brief The widget drawn with the pointer over the plot area. */
drawn hovered_frame(const view_verdict& verdict) {
    ImGui::GetIO().MousePos = area_centre;
    static_cast<void>(widget_frame(verdict));
    auto result = widget_frame(verdict);
    ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    return result;
}

/** @brief How many vertices a frame spent on each colour class. */
struct marks final {
    std::size_t quiet;
    std::size_t error;
    std::size_t spinner;
};

[[nodiscard]] marks marks_of(const drawn& frame) {
    return {frame.of(theme::view_quiet_colour).size(),
        frame.of(theme::view_error_colour).size(),
        frame.of(theme::plot_spinner_colour).size()};
}

/**
 * @brief The vertex count of each mark, drawn alone.
 *
 * A mark is cut into a fixed number of segments, so its vertex count is its
 * shape and does not change with the size it is drawn at. A window is judged
 * against these.
 */
struct shapes final {
    std::size_t not_connected;
    std::size_t error;
    std::size_t no_data;

    shapes()
        : not_connected(marks_of(widget_frame(not_connected_verdict)).quiet),
          error(marks_of(widget_frame(error_verdict)).error),
          no_data(marks_of(widget_frame(no_data_verdict)).quiet) {}
};

/** @brief The state a window's frame drew, judged by colour and shape. */
[[nodiscard]] view_state drew(const drawn& frame, const shapes& shape) {
    const auto m = marks_of(frame);
    /*
     * The colour says error or quiet, and the vertex count says which quiet
     * mark. A window may spend a rectangle of its own on the same grey, and a
     * card clips a little of a mark, so the count is matched to the nearest
     * shape rather than exactly.
     */
    const auto nearest = [](std::size_t got, std::size_t a, std::size_t b) {
        const auto gap = [got](std::size_t of) {
            return got > of ? got - of : of - got;
        };
        return gap(a) <= gap(b);
    };
    if (m.spinner > 0)
        return view_state::reading;
    if (m.error >= shape.error / 2)
        return view_state::error;
    if (m.quiet >= shape.no_data / 2)
        return nearest(m.quiet, shape.not_connected, shape.no_data)
            ? view_state::not_connected : view_state::no_data;
    return view_state::none;
}

[[nodiscard]] drawn window_frame(hedgr::testing::desk& desk,
    model::state::window_handle handle, render::window_ui_state& state,
    ImVec2 pointer = ImVec2(-FLT_MAX, -FLT_MAX)) {
    widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    input.wall_now_ns = 1'800'000'000'000'000'000;
    const auto kind = desk.windows.kind(handle);
    const auto draw = [&] {
        return capture([&] {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(
                kind == window_kind::mini_plot ? ImVec2(240.0f, 180.0f)
                                               : ImVec2(1000.0f, 700.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(0.0f, 0.0f));
            ImGui::Begin("window", nullptr, ImGuiWindowFlags_NoDecoration);
            render::renderer_for(kind)(handle, input, state, status, lock);
            ImGui::End();
            ImGui::PopStyleVar();
        });
    };
    ImGui::GetIO().MousePos = pointer;
    static_cast<void>(draw());
    auto result = draw();
    ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    return result;
}

/** @brief Opens a window of @p kind that no session is bound to. */
[[nodiscard]] model::state::window_handle open_unbound(
    hedgr::testing::desk& desk, window_kind kind,
    model::domain::window_state shape) {
    const auto handle = desk.windows.insert(model::domain::window_spec{
        .id = "unbound", .kind = kind, .state = std::move(shape)});
    desk.windows.set_open(handle, true);
    return handle;
}

[[nodiscard]] model::state::window_handle open_chart(
    hedgr::testing::desk& desk, window_kind kind) {
    if (kind == window_kind::chart)
        return desk.open(kind, model::domain::chart_state{
            .label = "FX GBP/USD", .series_id = "s"});
    return desk.open(kind, model::domain::mini_plot_state{
        .label = "FX GBP/USD", .series_id = "s"});
}

/** @brief Holds a series row for @p handle that nothing has read yet. */
[[nodiscard]] model::state::series_handle hold_series(
    hedgr::testing::desk& desk, model::state::window_handle handle) {
    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    return series;
}

constexpr ImVec2 body_centre(500.0f, 300.0f);

}

TEST_CASE("view_state_of judges a window body from its facts, row by row",
    "[view_state]") {
    struct row final {
        const char* name;
        view_facts facts;
        view_state state;
        view_words words;
        std::string_view reason;
    };
    const auto signed_in = [](view_facts f) {
        f.session = session_status::authenticated;
        return f;
    };

    const std::vector<row> table{
        {"no session bound", {}, view_state::not_connected,
            view_words::not_connected, {}},
        {"the session ended", {.ended = true}, view_state::not_connected,
            view_words::session_ended, {}},
        {"disconnected", {.session = session_status::disconnected},
            view_state::not_connected, view_words::not_connected, {}},
        {"connecting", {.session = session_status::connecting},
            view_state::reading, view_words::none, {}},
        {"a failed session carries its last error",
            {.session = session_status::failed, .session_error = "no route"},
            view_state::error, view_words::connection_failed, "no route"},
        {"authenticated without a token",
            signed_in({.has_token = false}), view_state::not_connected,
            view_words::not_connected, {}},
        {"a read in flight", signed_in({.reading = true}),
            view_state::reading, view_words::none, {}},
        {"a failed read", signed_in({.read_failure = "refused"}),
            view_state::error, view_words::read_failed, "refused"},
        {"a read that holds nothing",
            signed_in({.has_data = false, .empty_what = "no rates"}),
            view_state::no_data, view_words::empty, "no rates"},
        {"content", signed_in({}), view_state::none, view_words::none, {}},
    };
    for (const auto& r : table) {
        INFO(r.name);
        const auto verdict = widget::view_state_of(r.facts);
        CHECK(verdict.state == r.state);
        CHECK(verdict.words == r.words);
        CHECK(verdict.reason == r.reason);
    }
}

TEST_CASE("the session decides the body before the window's own read does",
    "[view_state]") {
    const auto verdict = widget::view_state_of({
        .session = session_status::disconnected, .reading = true,
        .read_failure = "refused", .has_data = false});
    CHECK(verdict.state == view_state::not_connected);

    const auto failed = widget::view_state_of({
        .session = session_status::failed, .session_error = "no route",
        .has_data = false});
    CHECK(failed.state == view_state::error);
    CHECK(failed.words == view_words::connection_failed);

    const auto reading = widget::view_state_of({
        .session = session_status::authenticated, .reading = true,
        .read_failure = "stale", .has_data = false});
    CHECK(reading.state == view_state::reading);

    const auto failure = widget::view_state_of({
        .session = session_status::authenticated, .read_failure = "refused",
        .has_data = false});
    CHECK(failure.state == view_state::error);

    /* A failure that left data on screen is still an error, for the foot. */
    const auto stale = widget::view_state_of({
        .session = session_status::authenticated, .read_failure = "refused",
        .has_data = true});
    CHECK(stale.state == view_state::error);
}

TEST_CASE("every state has its own foot word, from one table", "[view_state]") {
    CHECK(widget::view_foot_word(view_state::not_connected) == "OFFLINE");
    CHECK(widget::view_foot_word(view_state::error) == "ERROR");
    CHECK(widget::view_foot_word(view_state::no_data) == "NO DATA");
    CHECK(widget::view_foot_word(view_state::reading).empty());
    CHECK(widget::view_foot_word(view_state::none).empty());

    hedgr::testing::headless_imgui imgui;
    widget::foot_state foot;
    widget::set_view_foot(foot, not_connected_verdict);
    CHECK(foot.word == "OFFLINE");
    widget::set_view_foot(foot, error_verdict);
    CHECK(foot.word == "ERROR");
    CHECK(foot.colour == theme::failure_colour);
    CHECK(foot.detail == "bad password");
    widget::set_view_foot(foot, no_data_verdict);
    CHECK(foot.word == "NO DATA");

    widget::foot_state live;
    live.word = "LIVE";
    widget::set_view_foot(live, {});
    CHECK(live.word == "LIVE");
    widget::set_view_foot(live, reading_verdict);
    CHECK(live.word == "LIVE");
    CHECK(live.updating);
}

TEST_CASE("every state's tooltip opens with its own sentence", "[view_state]") {
    using widget::tooltip_of;
    const auto off = tooltip_of(not_connected_verdict);
    CHECK(off.caption == "Not connected");
    CHECK(off.detail == "Sign in from the Accounts window.");
    const auto ended = tooltip_of({view_state::not_connected,
        view_words::session_ended, {}});
    CHECK(ended.caption == "This session has ended");
    CHECK(ended.detail == "Sign in again from the Accounts window.");
    const auto failed = tooltip_of(error_verdict);
    CHECK(failed.caption == "Connection failed");
    CHECK(failed.detail == "bad password");
    const auto read = tooltip_of({view_state::error, view_words::read_failed,
        "no answer"});
    CHECK(read.caption == "Could not read the data");
    CHECK(read.detail == "no answer");
    const auto empty = tooltip_of(no_data_verdict);
    CHECK(empty.caption == "No data");
    CHECK(empty.detail == "This series holds no points.");
    CHECK(tooltip_of(reading_verdict).caption.empty());
    CHECK(tooltip_of({}).caption.empty());
}

TEST_CASE("the quiet and error marks are faint neutral greys, the error one a"
          " step brighter",
    "[view_state]") {
    const auto red = [](ImU32 c) { return static_cast<int>(c & 0xFF); };
    const auto green = [](ImU32 c) { return static_cast<int>((c >> 8) & 0xFF); };
    const auto blue = [](ImU32 c) { return static_cast<int>((c >> 16) & 0xFF); };
    CHECK(red(theme::view_error_colour) > red(theme::view_quiet_colour));
    for (const ImU32 c : {theme::view_quiet_colour, theme::view_error_colour}) {
        CHECK(red(c) < 90);
        CHECK(blue(c) < 90);
        CHECK(std::abs(red(c) - green(c)) <= 6);
        CHECK(std::abs(blue(c) - green(c)) <= 12);
    }
}

TEST_CASE("a view state draws its own colour class and no other",
    "[view_state]") {
    hedgr::testing::headless_imgui imgui;

    const auto off = marks_of(widget_frame(not_connected_verdict));
    CHECK(off.quiet > 0);
    CHECK(off.error == 0);
    CHECK(off.spinner == 0);

    const auto failed = marks_of(widget_frame(error_verdict));
    CHECK(failed.quiet == 0);
    CHECK(failed.error > 0);
    CHECK(failed.spinner == 0);

    const auto empty = marks_of(widget_frame(no_data_verdict));
    CHECK(empty.quiet > 0);
    CHECK(empty.error == 0);
    CHECK(empty.spinner == 0);

    const auto reading = marks_of(widget_frame(reading_verdict));
    CHECK(reading.quiet == 0);
    CHECK(reading.error == 0);
    CHECK(reading.spinner > 0);

    const auto none = widget_frame({});
    const auto nothing = marks_of(none);
    CHECK(nothing.quiet + nothing.error + nothing.spinner == 0);
}

TEST_CASE("the three marks are distinct in colour and in shape",
    "[view_state]") {
    hedgr::testing::headless_imgui imgui;
    const auto off = widget_frame(not_connected_verdict);
    const auto failed = widget_frame(error_verdict);
    const auto empty = widget_frame(no_data_verdict);

    /* The error mark is the only one in the brighter grey. */
    CHECK(off.of(theme::view_error_colour).empty());
    CHECK(empty.of(theme::view_error_colour).empty());
    CHECK(failed.of(theme::view_quiet_colour).empty());

    const auto off_at = off.of(theme::view_quiet_colour);
    const auto failed_at = failed.of(theme::view_error_colour);
    const auto empty_at = empty.of(theme::view_quiet_colour);
    REQUIRE_FALSE(off_at.empty());
    REQUIRE_FALSE(failed_at.empty());
    REQUIRE_FALSE(empty_at.empty());

    const std::vector<std::size_t> counts{
        off_at.size(), failed_at.size(), empty_at.size()};
    CHECK(counts[0] != counts[1]);
    CHECK(counts[0] != counts[2]);
    CHECK(counts[1] != counts[2]);

    const auto close = [](ImVec2 a, ImVec2 b) {
        return std::abs(a.x - b.x) < 2.0f && std::abs(a.y - b.y) < 2.0f;
    };
    CHECK_FALSE(close(extent_of(off_at), extent_of(failed_at)));
    CHECK_FALSE(close(extent_of(off_at), extent_of(empty_at)));
    CHECK_FALSE(close(extent_of(failed_at), extent_of(empty_at)));

    /* The error mark is a ring, so it is as wide as it is tall. */
    const auto ring = extent_of(failed_at);
    CHECK(std::abs(ring.x - ring.y) < 2.0f);
}

TEST_CASE("each mark sits in the middle of the area and is drawn large",
    "[view_state]") {
    hedgr::testing::headless_imgui imgui;
    const float shorter = std::min(area_max.x - area_min.x,
        area_max.y - area_min.y);

    for (const auto& [verdict, colour] : {
             std::pair{not_connected_verdict, theme::view_quiet_colour},
             std::pair{error_verdict, theme::view_error_colour},
             std::pair{no_data_verdict, theme::view_quiet_colour}}) {
        const auto at = widget_frame(verdict).of(colour);
        REQUIRE_FALSE(at.empty());
        const ImVec2 middle = centre_of(at);
        CHECK(std::abs(middle.x - area_centre.x) < 12.0f);
        CHECK(std::abs(middle.y - area_centre.y) < 12.0f);
        const auto size = extent_of(at);
        CHECK(size.y > shorter * 0.25f);
    }

    const auto ring = widget_frame(reading_verdict)
        .of(theme::plot_spinner_colour);
    const float reach = theme::view_state_glyph_size * 0.5f
        + theme::plot_spinner_weight;
    for (const auto p : ring)
        CHECK(std::hypot(p.x - area_centre.x, p.y - area_centre.y) <= reach);
}

TEST_CASE("the spinner turns with the clock and a mark is still",
    "[view_state]") {
    hedgr::testing::headless_imgui imgui;
    const auto first = widget_frame(reading_verdict)
        .of(theme::plot_spinner_colour);
    for (int i = 0; i < 10; ++i)
        static_cast<void>(widget_frame(reading_verdict));
    const auto later = widget_frame(reading_verdict)
        .of(theme::plot_spinner_colour);

    REQUIRE(first.size() == later.size());
    bool moved = false;
    for (std::size_t i = 0; i < first.size(); ++i)
        moved = moved || std::abs(first[i].x - later[i].x) > 0.5f
            || std::abs(first[i].y - later[i].y) > 0.5f;
    CHECK(moved);

    const auto a = widget_frame(error_verdict).of(theme::view_error_colour);
    const auto b = widget_frame(error_verdict).of(theme::view_error_colour);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].x == b[i].x);
        CHECK(a[i].y == b[i].y);
    }
}

TEST_CASE("a mark draws no words, they are in a tooltip over it",
    "[view_state]") {
    hedgr::testing::headless_imgui imgui;

    for (const auto& verdict : {not_connected_verdict, error_verdict,
             no_data_verdict, reading_verdict}) {
        const auto resting = widget_frame(verdict);
        CHECK(resting.text.find_first_not_of(" \n") == std::string::npos);
    }

    const auto off = hovered_frame(not_connected_verdict);
    CHECK(off.says("Not connected"));
    CHECK(off.says("Sign in from the Accounts window."));
    const auto ended = hovered_frame(
        {view_state::not_connected, view_words::session_ended, {}});
    CHECK(ended.says("This session has ended"));
    CHECK(ended.says("Sign in again from the Accounts window."));
    CHECK_FALSE(ended.says("Not connected"));
    const auto failed = hovered_frame(error_verdict);
    CHECK(failed.says("Connection failed"));
    CHECK(failed.says("bad password"));
    CHECK_FALSE(failed.says("Not connected"));
    const auto read = hovered_frame(
        {view_state::error, view_words::read_failed, "no answer"});
    CHECK(read.says("Could not read the data"));
    CHECK(read.says("no answer"));
    const auto empty = hovered_frame(no_data_verdict);
    CHECK(empty.says("No data"));
    CHECK(empty.says("This series holds no points."));
    CHECK_FALSE(empty.says("Not connected"));
    const auto spinning = hovered_frame(reading_verdict);
    CHECK(spinning.text.find_first_not_of(" \n") == std::string::npos);
}

TEST_CASE("a chart shows the mark its session and its read call for",
    "[view_state][chart]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = open_chart(desk, window_kind::chart);
    const auto series = hold_series(desk, handle);
    render::window_ui_state state;

    const auto unbound = open_unbound(desk, window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});
    const auto never = window_frame(desk, unbound, state);
    CHECK(drew(never, shape) == view_state::not_connected);
    CHECK(never.says("OFFLINE"));
    CHECK_FALSE(never.says("Not connected"));
    CHECK_FALSE(never.says("Sign in"));
    const auto hover = window_frame(desk, unbound, state, body_centre);
    CHECK(hover.says("Not connected"));
    CHECK(hover.says("Sign in from the Accounts window."));

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(drew(off, shape) == view_state::not_connected);
    CHECK(off.says("OFFLINE"));
    CHECK_FALSE(off.says("NO DATA"));

    desk.sessions.set_status(desk.session, session_status::failed);
    desk.sessions.set_last_error(desk.session, "certificate rejected");
    const auto failed = window_frame(desk, handle, state);
    CHECK(drew(failed, shape) == view_state::error);
    CHECK(failed.says("ERROR"));
    CHECK_FALSE(failed.says("certificate rejected"));
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("certificate rejected"));

    desk.sessions.set_status(desk.session, session_status::connecting);
    const auto connecting = window_frame(desk, handle, state);
    CHECK(drew(connecting, shape) == view_state::reading);
    CHECK_FALSE(connecting.says("Reading the series"));

    desk.sessions.set_status(desk.session, session_status::authenticated);
    desk.sessions.set_token(desk.session, model::domain::secret_string{""});
    const auto tokenless = window_frame(desk, handle, state);
    CHECK(drew(tokenless, shape) == view_state::not_connected);

    desk.sessions.set_token(desk.session, model::domain::secret_string{"t"});
    const auto reading = window_frame(desk, handle, state);
    CHECK(drew(reading, shape) == view_state::reading);

    desk.market.fail(series, "no such series", 1);
    const auto broken = window_frame(desk, handle, state);
    CHECK(drew(broken, shape) == view_state::error);
    CHECK(broken.says("ERROR"));
    CHECK_FALSE(broken.says("History unavailable"));
    CHECK_FALSE(broken.says("no such series"));
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("no such series"));
}

TEST_CASE("a chart whose series was read and holds no points shows the"
          " no-data mark",
    "[view_state][chart]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = open_chart(desk, window_kind::chart);
    const auto series = hold_series(desk, handle);
    static_cast<void>(desk.market.seed(series, {}, 1));
    render::window_ui_state state;

    const auto empty = window_frame(desk, handle, state);
    CHECK(drew(empty, shape) == view_state::no_data);
    CHECK(empty.says("NO DATA"));
    CHECK_FALSE(empty.says("No observations for this series yet."));
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("This series holds no points."));
}

TEST_CASE("a card shows the mark alone for each state, under its header",
    "[view_state][mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = open_chart(desk, window_kind::mini_plot);
    const auto series = hold_series(desk, handle);
    render::window_ui_state state;

    const auto expect_bare = [](const drawn& card) {
        CHECK(card.says("FX GBP/USD"));
        CHECK_FALSE(card.says("Not connected"));
        CHECK_FALSE(card.says("Reading"));
        CHECK_FALSE(card.says("Series not held"));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    expect_bare(off);
    CHECK(drew(off, shape) == view_state::not_connected);

    desk.sessions.set_status(desk.session, session_status::failed);
    desk.sessions.set_last_error(desk.session, "certificate rejected");
    const auto failed = window_frame(desk, handle, state);
    expect_bare(failed);
    CHECK_FALSE(failed.says("certificate rejected"));
    CHECK(drew(failed, shape) == view_state::error);

    desk.sessions.set_status(desk.session, session_status::connecting);
    expect_bare(window_frame(desk, handle, state));
    CHECK(drew(window_frame(desk, handle, state), shape)
        == view_state::reading);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    CHECK(drew(window_frame(desk, handle, state), shape)
        == view_state::reading);

    desk.market.fail(series, "no such series", 1);
    const auto broken = window_frame(desk, handle, state);
    expect_bare(broken);
    CHECK(drew(broken, shape) == view_state::error);
    CHECK(window_frame(desk, handle, state, ImVec2(120.0f, 120.0f))
              .says("no such series"));

    const auto unheld = desk.open(window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD",
            .series_id = "u"},
        "unheld");
    const auto missing = window_frame(desk, unheld, state);
    expect_bare(missing);
    CHECK(drew(missing, shape) == view_state::error);
    CHECK(window_frame(desk, unheld, state, ImVec2(120.0f, 120.0f))
              .says("This series is not in the catalogue."));
}

TEST_CASE("a card's mark is centred below its header, inside the card",
    "[view_state][mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = open_chart(desk, window_kind::mini_plot);
    static_cast<void>(hold_series(desk, handle));
    render::window_ui_state state;
    desk.sessions.set_status(desk.session, session_status::disconnected);

    const auto mark = window_frame(desk, handle, state)
        .of(theme::view_quiet_colour);
    REQUIRE_FALSE(mark.empty());
    const ImVec2 at = centre_of(mark);
    CHECK(std::abs(at.x - 120.0f) < 8.0f);
    CHECK(at.y > 90.0f);
    CHECK(at.y < 170.0f);
}

TEST_CASE("a CRM window shows the mark for each of its states and none of"
          " its old sentences",
    "[view_state][crm]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::crm_matrix,
        model::domain::crm_matrix_state{.crm_name = "majors"});
    render::window_ui_state state;
    const auto no_sentences = [](const drawn& frame) {
        for (const auto words : {"Reading the CRM", "No rates configured",
                 "No CRM selected", "No pairs for this CRM", "Not connected"})
            CHECK_FALSE(frame.says(words));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(drew(off, shape) == view_state::not_connected);
    CHECK(off.says("OFFLINE"));
    no_sentences(off);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    const auto reading = window_frame(desk, handle, state);
    CHECK(drew(reading, shape) == view_state::reading);
    no_sentences(reading);

    desk.crm.begin("i", "a", "", "majors", false);
    desk.crm.fail("i", "a", "", "majors", false, "the matrix was refused");
    const auto failed = window_frame(desk, handle, state);
    CHECK(drew(failed, shape) == view_state::error);
    CHECK(failed.says("ERROR"));
    CHECK_FALSE(failed.says("the matrix was refused"));
    no_sentences(failed);

    desk.crm.set("i", "a", "", "majors", false, {}, {}, 1, 1);
    const auto empty = window_frame(desk, handle, state);
    CHECK(drew(empty, shape) == view_state::no_data);
    CHECK(empty.says("NO DATA"));
    no_sentences(empty);
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("No rates are configured for this party."));

    const auto unselected = desk.open(window_kind::crm_matrix,
        model::domain::crm_matrix_state{}, "unselected");
    const auto none = window_frame(desk, unselected, state);
    CHECK(drew(none, shape) == view_state::no_data);
    no_sentences(none);
}

TEST_CASE("a term structure window shows the mark for each of its states",
    "[view_state][term]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::term_structure,
        model::domain::term_structure_state{.label = "IR USD", .curve_key = "c"});
    render::window_ui_state state;
    const auto no_sentences = [](const drawn& frame) {
        CHECK_FALSE(frame.says("Reading the curve"));
        CHECK_FALSE(frame.says("Not connected"));
        CHECK_FALSE(frame.says("the curve was refused"));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(drew(off, shape) == view_state::not_connected);
    CHECK(off.says("OFFLINE"));
    no_sentences(off);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    const auto reading = window_frame(desk, handle, state);
    CHECK(drew(reading, shape) == view_state::reading);
    no_sentences(reading);

    const auto curve_row = desk.curves.intern("i", "a", "c");
    desk.curves.fail(curve_row, "the curve was refused");
    const auto failed = window_frame(desk, handle, state);
    CHECK(drew(failed, shape) == view_state::error);
    CHECK(failed.says("ERROR"));
    no_sentences(failed);

    desk.curves.apply(curve_row, "IR USD", {}, {}, {});
    const auto empty = window_frame(desk, handle, state);
    CHECK(drew(empty, shape) == view_state::no_data);
    CHECK(empty.says("NO DATA"));
    no_sentences(empty);
}

TEST_CASE("the market data browser shows the mark for each catalogue state",
    "[view_state][series_browser]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    const auto no_sentences = [](const drawn& frame) {
        for (const auto words : {"Reading the market series",
                 "This deployment holds no market series", "Not connected",
                 "No series match", "the catalogue was refused"})
            CHECK_FALSE(frame.says(words));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(drew(off, shape) == view_state::not_connected);
    CHECK(off.says("OFFLINE"));
    no_sentences(off);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    desk.market.begin_catalogue("i", "a");
    const auto reading = window_frame(desk, handle, state);
    CHECK(drew(reading, shape) == view_state::reading);
    no_sentences(reading);

    desk.market.fail_catalogue("i", "a", "the catalogue was refused");
    const auto failed = window_frame(desk, handle, state);
    CHECK(drew(failed, shape) == view_state::error);
    CHECK(failed.says("ERROR"));
    no_sentences(failed);

    desk.market.apply_catalogue("i", "a", {});
    const auto empty = window_frame(desk, handle, state);
    CHECK(drew(empty, shape) == view_state::no_data);
    CHECK(empty.says("NO DATA"));
    no_sentences(empty);

    std::vector<model::domain::market_series_row> rows(1);
    rows[0].id = "s-1";
    rows[0].label = "FX GBP/USD";
    rows[0].asset_class = "FX";
    desk.market.apply_catalogue("i", "a", rows);
    state.filter[0] = 'z';
    const auto unmatched = window_frame(desk, handle, state);
    CHECK(drew(unmatched, shape) == view_state::no_data);
    no_sentences(unmatched);
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("No series match the filters."));
    state.filter[0] = '\0';
    CHECK(drew(window_frame(desk, handle, state), shape) == view_state::none);
}

TEST_CASE("the command centre shows the mark for each of its states",
    "[view_state][command_centre]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = desk.open(window_kind::command_centre,
        model::domain::command_centre_state{.instance_id = "i"});
    render::window_ui_state state;
    const auto no_sentences = [](const drawn& frame) {
        CHECK_FALSE(frame.says("Reading the deployment"));
        CHECK_FALSE(frame.says("Not connected"));
        CHECK_FALSE(frame.says("tenant list refused"));
    };

    desk.sessions.set_status(desk.session, session_status::disconnected);
    const auto off = window_frame(desk, handle, state);
    CHECK(drew(off, shape) == view_state::not_connected);
    CHECK(off.says("OFFLINE"));
    no_sentences(off);

    desk.sessions.set_status(desk.session, session_status::authenticated);
    const auto reading = window_frame(desk, handle, state);
    CHECK(drew(reading, shape) == view_state::reading);
    no_sentences(reading);

    model::domain::deployment_snapshot failing;
    failing.failure = "tenant list refused";
    failing.failing_read = "tenants";
    desk.sessions.set_deployment(desk.session, failing);
    const auto failed = window_frame(desk, handle, state);
    CHECK(drew(failed, shape) == view_state::error);
    CHECK(failed.says("ERROR"));
    no_sentences(failed);
    CHECK(window_frame(desk, handle, state, body_centre)
              .says("While reading tenants: tenant list refused"));
}

TEST_CASE("a workspace whose session ended shows the ended mark, not a sentence",
    "[view_state][workspace]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    const shapes shape;
    hedgr::testing::desk desk;
    const auto handle = open_unbound(desk, window_kind::workspace,
        model::domain::workspace_state{.account_id = "a"});
    render::window_ui_state state;

    const auto frame = window_frame(desk, handle, state);
    CHECK(drew(frame, shape) == view_state::not_connected);
    CHECK_FALSE(frame.says("This session has ended"));
    CHECK_FALSE(frame.says("Sign in again"));
    CHECK(frame.says("OFFLINE"));

    const auto hover = window_frame(desk, handle, state, body_centre);
    CHECK(hover.says("This session has ended"));
    CHECK(hover.says("Sign in again from the Accounts window."));
    CHECK_FALSE(hover.says("Not connected"));
}

/**
 * Not a test: with HEDGR_VIEW_STATE_PPM set, writes the not-connected, error
 * and no-data marks side by side over the plot background to a PPM, so the
 * shapes can be looked at without opening a window.
 */
TEST_CASE("view state capture", "[.gallery]") {
    const char* out = std::getenv("HEDGR_VIEW_STATE_PPM");
    REQUIRE(out != nullptr);
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::software_raster raster(1350, 400);
    for (int pass = 0; pass < 3; ++pass) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1350.0f, 400.0f));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        widget::draw_view_state(not_connected_verdict, ImVec2(0, 0),
            ImVec2(450, 400), theme::view_state_glyph_size);
        widget::draw_view_state(error_verdict, ImVec2(450, 0),
            ImVec2(900, 400), theme::view_state_glyph_size);
        widget::draw_view_state(no_data_verdict, ImVec2(900, 0),
            ImVec2(1350, 400), theme::view_state_glyph_size);
        ImGui::End();
        ImGui::Render();
        raster.update_textures();
    }
    raster.draw(*ImGui::GetDrawData(), IM_COL32(15, 20, 32, 255));
    CHECK(raster.write_ppm(std::filesystem::path(out) / "view-state.ppm"));
}
