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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/mini_plot.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "software_raster.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;

constexpr std::int64_t now_ns = 1'760'000'000'000'000'000;
constexpr std::int64_t step_ns = 600'000'000'000;
constexpr std::int64_t card_span_ns = 24LL * 3600 * 1'000'000'000;

/** @brief A card whose series holds @p count points ending now, shaped by @p value. */
template<class Value>
model::state::window_handle card_with(hedgr::testing::desk& desk,
    std::size_t count, Value value, std::string id = "s",
    bool also_fine = false) {
    const auto handle = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = id,
            .span_ns = card_span_ns},
        "card-" + id);
    const auto series = desk.market.intern("i", "a", id);
    desk.windows.set_series(handle, std::span{&series, 1});
    for (std::size_t k = 0; k < count; ++k) {
        const model::state::series_point point{.at_ns = now_ns
                - static_cast<std::int64_t>(count - 1 - k) * step_ns,
            .value = value(k)};
        desk.market.append(series,
            model::domain::market_level_for_span(card_span_ns), point, 1);
        if (also_fine)
            desk.market.append(series, model::domain::market_level::fine,
                point, 1);
    }
    return handle;
}

/** @brief What a card put in its window's draw list. */
struct card_draw final {
    int line_vertices = 0;
    int fill_vertices = 0;
    int total_vertices = 0;
    ImVec4 window_clip;
    float spark_bottom = -FLT_MAX;
    float pill_top = FLT_MAX;
};

[[nodiscard]] bool same_rgb(ImU32 a, ImU32 b) {
    return (a & 0x00FFFFFFu) == (b & 0x00FFFFFFu);
}

/** @brief Draws the card in a window of @p size and counts what it drew. */
card_draw draw_card(hedgr::testing::desk& desk,
    model::state::window_handle handle, theme::tone tone, ImVec2 size,
    ImU32 colour = ImGui::ColorConvertFloat4ToU32(theme::chart_line_colour())) {
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    render::window_ui_state state;
    auto input = desk.input();
    input.wall_now_ns = now_ns;
    card_draw result;
    for (int pass = 0; pass < 2; ++pass) {
        result = {};
        ImGui::NewFrame();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("card", nullptr, ImGuiWindowFlags_NoDecoration);
        render::renderer_for(model::domain::window_kind::mini_plot)(
            handle, input, state, status, lock);
        const ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 white = draw->_Data->TexUvWhitePixel;
        const ImU32 pill_fill = theme::pill_colours(tone).fill;
        /*
         * The line and its fill are the first run of the tone's colour in the
         * list. The pill shares that colour at other alphas and is drawn after
         * the header, so the run ends before it.
         */
        bool in_run = false;
        bool run_done = false;
        for (int i = 0; i < draw->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = draw->VtxBuffer[i];
            const bool white_tone = v.uv.x == white.x && v.uv.y == white.y
                && same_rgb(v.col, colour);
            if (white_tone && !run_done) {
                in_run = true;
                result.spark_bottom = std::max(result.spark_bottom, v.pos.y);
            } else if (in_run) {
                run_done = true;
            }
            if (v.uv.x == white.x && v.uv.y == white.y && v.col == pill_fill)
                result.pill_top = std::min(result.pill_top, v.pos.y);
            if (v.uv.x != white.x || v.uv.y != white.y || !same_rgb(v.col, colour))
                continue;
            if ((v.col >> 24) == 255)
                ++result.line_vertices;
            else
                ++result.fill_vertices;
        }
        result.total_vertices = draw->VtxBuffer.Size;
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
    }
    return result;
}

std::string drawn_text(hedgr::testing::desk& desk,
    model::state::window_handle handle) {
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    render::window_ui_state state;
    auto input = desk.input();
    std::string text;
    hedgr::testing::frame([&] {
        ImGui::LogToBuffer();
        render::renderer_for(model::domain::window_kind::mini_plot)(
            handle, input, state, status, lock);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });
    return text;
}

}

TEST_CASE("a mini plot card names its pair and, once held, stops reading",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = "s"});

    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    desk.market.append(series, model::domain::market_level::fine,
        {.at_ns = 1'000'000'000, .value = 1.25}, 1);

    const auto text = drawn_text(desk, handle);
    CHECK(text.find("FX GBP/USD") != std::string::npos);
    CHECK(text.find("Reading the series") == std::string::npos);
}

TEST_CASE("a mini plot card for a series not held says so",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = "s"});

    const auto text = drawn_text(desk, handle);
    CHECK(text.find("Series not held") != std::string::npos);
}

TEST_CASE("a card's line and fill are the chart's colour whatever the series did",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto rising = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; },
        "up");
    const auto falling = card_with(desk, 60,
        [](std::size_t k) { return 1.30 - static_cast<double>(k) * 0.001; },
        "down");

    for (const auto& [handle, tone] :
        {std::pair{rising, theme::tone::good},
            std::pair{falling, theme::tone::bad}}) {
        const auto drawn = draw_card(desk, handle, tone, ImVec2(260.0f, 170.0f));
        CHECK(drawn.line_vertices > 0);
        CHECK(drawn.fill_vertices > 0);
        const auto verdict = draw_card(desk, handle, tone,
            ImVec2(260.0f, 170.0f), theme::pill_colours(tone).text);
        CHECK(verdict.line_vertices == 0);
        /* The pill still carries the verdict, and only the pill. */
        CHECK(drawn.pill_top < FLT_MAX);
        const auto other = tone == theme::tone::good ? theme::tone::bad
                                                     : theme::tone::good;
        CHECK(draw_card(desk, handle, other, ImVec2(260.0f, 170.0f)).pill_top
            == FLT_MAX);
    }
}

namespace {

constexpr std::int64_t minute_ns = 60'000'000'000;

/** @brief A chart window plotting the series @p card holds, on span @p span_ns. */
model::state::window_handle chart_beside(hedgr::testing::desk& desk,
    model::state::window_handle card, std::int64_t span_ns,
    std::string id = "chart") {
    const auto chart = desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s",
            .view = model::domain::chart_view{.span_ns = span_ns}},
        id);
    desk.windows.set_series(chart, desk.windows.series(card));
    return chart;
}

}

TEST_CASE("a card with no chart open shows the span it was opened with",
    "[mini_plot][span]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; });

    CHECK(render::mini_plot_span(desk.input(), handle) == card_span_ns);
    const auto text = drawn_text(desk, handle);
    CHECK(text.find("1d") != std::string::npos);
    CHECK(text.find("24H") == std::string::npos);
}

TEST_CASE("a card follows the span of the open chart of its series",
    "[mini_plot][span]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; },
        "s", true);
    const auto chart = chart_beside(desk, handle, 15 * minute_ns);

    CHECK(render::mini_plot_span(desk.input(), handle) == 15 * minute_ns);
    const auto text = drawn_text(desk, handle);
    CHECK(text.find("15m") != std::string::npos);
    CHECK(text.find("1d") == std::string::npos);

    /* The chart changes period and the card changes with it. */
    std::get<model::domain::chart_state>(desk.windows.state(chart)).view->span_ns
        = 60 * minute_ns;
    CHECK(render::mini_plot_span(desk.input(), handle) == 60 * minute_ns);
    CHECK(drawn_text(desk, handle).find("1h") != std::string::npos);
}

TEST_CASE("a closed chart, or a chart of another series, does not move a card",
    "[mini_plot][span]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; });

    const auto closed = chart_beside(desk, handle, 15 * minute_ns, "closed");
    desk.windows.set_open(closed, false);
    const auto other = chart_beside(desk, handle, 60 * minute_ns, "other");
    const auto elsewhere = desk.market.intern("i", "a", "elsewhere");
    desk.windows.set_series(other, std::span{&elsewhere, 1});

    CHECK(render::mini_plot_span(desk.input(), handle) == card_span_ns);
}

TEST_CASE("a card with no points yet or no held series draws no line",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto unheld = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = "u"},
        "unheld");
    const auto empty = card_with(desk, 0,
        [](std::size_t) { return 1.0; }, "e");
    const auto one = card_with(desk, 1, [](std::size_t) { return 1.0; }, "o");

    for (const auto handle : {unheld, empty, one}) {
        for (const auto tone : {theme::tone::good, theme::tone::bad,
                 theme::tone::neutral}) {
            const auto drawn = draw_card(desk, handle, tone,
                ImVec2(260.0f, 170.0f));
            CHECK(drawn.line_vertices == 0);
            CHECK(drawn.fill_vertices == 0);
        }
    }
    CHECK(drawn_text(desk, unheld).find("Series not held") != std::string::npos);
    CHECK(drawn_text(desk, empty).find("Reading the series") != std::string::npos);
}

TEST_CASE("a card scales from tiny to large and keeps its line inside its tile",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; });

    for (const ImVec2 size : {ImVec2(160.0f, 110.0f), ImVec2(90.0f, 60.0f),
             ImVec2(40.0f, 30.0f), ImVec2(900.0f, 600.0f)}) {
        INFO("size " << size.x << "x" << size.y);
        const auto drawn = draw_card(desk, handle, theme::tone::good, size);
        CHECK(drawn.line_vertices > 0);
        CHECK(drawn.line_vertices < 1000);
    }
}

TEST_CASE("a card's line stops above the change pill at every size",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    ImGui::GetIO().DisplaySize = ImVec2(1000.0f, 700.0f);
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; });

    for (const ImVec2 size : {ImVec2(160.0f, 110.0f), ImVec2(200.0f, 140.0f),
             ImVec2(900.0f, 600.0f)}) {
        INFO("size " << size.x << "x" << size.y);
        const auto drawn = draw_card(desk, handle, theme::tone::good, size);
        REQUIRE(drawn.line_vertices > 0);
        REQUIRE(drawn.pill_top < FLT_MAX);
        CHECK(drawn.spark_bottom < drawn.pill_top);
    }
}

TEST_CASE("a card's line has the same cost for a short and a very long series",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto value = [](std::size_t k) {
        return 1.20 + static_cast<double>(k) * 0.0001
            + 0.003 * std::sin(static_cast<double>(k) * 0.7);
    };
    const auto few = card_with(desk, 400, value, "few");
    const auto many = card_with(desk, 5000, value, "many");
    const auto a = draw_card(desk, few, theme::tone::good, ImVec2(260.0f, 170.0f));
    const auto b = draw_card(desk, many, theme::tone::good, ImVec2(260.0f, 170.0f));
    INFO("vertices " << a.total_vertices << " vs " << b.total_vertices);
    CHECK(a.line_vertices > 0);
    CHECK(a.line_vertices == b.line_vertices);
    CHECK(a.fill_vertices == b.fill_vertices);
    CHECK(a.total_vertices == b.total_vertices);
}

TEST_CASE("the sparkline reduces any series to the point budget and the unit box",
    "[mini_plot]") {
    std::vector<double> at_s;
    std::vector<std::int64_t> at_ns;
    std::vector<double> value;
    for (std::size_t k = 0; k < 1000; ++k) {
        at_ns.push_back(now_ns + static_cast<std::int64_t>(k) * step_ns);
        at_s.push_back(static_cast<double>(at_ns.back()) / 1e9);
        value.push_back(static_cast<double>(k % 10));
    }
    model::state::series_columns points;
    points.at_s = at_s.data();
    points.at_ns = at_ns.data();
    points.value = value.data();
    points.size = at_ns.size();

    const auto line = render::make_sparkline(points, 0);
    REQUIRE(line.size == theme::mini_plot_point_budget);
    CHECK(line.x[0] == 0.0f);
    CHECK(line.x[line.size - 1] == Catch::Approx(1.0f));
    for (std::size_t k = 0; k < line.size; ++k) {
        CHECK(line.y[k] >= 0.0f);
        CHECK(line.y[k] <= 1.0f);
    }

    CHECK(render::make_sparkline(points, 999).size == 0);
    CHECK(render::make_sparkline(points, 5).size == theme::mini_plot_point_budget);
    points.size = 10;
    CHECK(render::make_sparkline(points, 0).size == 10);
    CHECK(render::mini_plot_first(points, card_span_ns) == 0);
    points.size = 1000;
    const auto first = render::mini_plot_first(points, 100 * step_ns);
    CHECK(first == 899);
}

namespace {

/**
 * @brief The vertices a window drew itself, of the colour @p colour, outside
 * @p box on the left, right or bottom. Above is the title bar, which stays.
 */
int outside(const ImGuiWindow* window, ImU32 colour, ImRect box) {
    box.Expand(1.5f);
    box.Min.y = -FLT_MAX;
    int count = 0;
    for (const ImDrawVert& v : window->DrawList->VtxBuffer)
        if (v.col == colour && !box.Contains(v.pos))
            ++count;
    return count;
}

/** @brief Checks that the card tile is the whole content region of @p id. */
void check_card_fills_window(const char* id) {
    const ImGuiWindow* w = ImGui::FindWindowByName(id);
    REQUIRE(w != nullptr);
    REQUIRE(w->Active);
    const ImU32 surface = ImGui::ColorConvertFloat4ToU32(theme::card_surface());
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    ImRect tile(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const ImDrawVert& v : w->DrawList->VtxBuffer)
        if (v.col == surface && v.uv.x == white.x && v.uv.y == white.y)
            tile.Add(v.pos);
    INFO(id << " tile " << tile.Min.x << "," << tile.Min.y << " " << tile.Max.x
            << "," << tile.Max.y << " inner " << w->InnerRect.Min.x << ","
            << w->InnerRect.Min.y << " " << w->InnerRect.Max.x << ","
            << w->InnerRect.Max.y);
    CHECK(std::abs(tile.Min.x - w->InnerRect.Min.x) <= 1.5f);
    CHECK(std::abs(tile.Min.y - w->InnerRect.Min.y) <= 1.5f);
    CHECK(std::abs(tile.Max.x - w->InnerRect.Max.x) <= 1.5f);
    CHECK(std::abs(tile.Max.y - w->InnerRect.Max.y) <= 1.5f);

    const ImRect inner = w->InnerRect;
    CHECK(outside(w, ImGui::GetColorU32(ImGuiCol_Border), inner) == 0);
    const ImU32 background = ImGui::GetColorU32(ImGuiCol_WindowBg);
    CHECK(std::ranges::none_of(w->DrawList->VtxBuffer,
        [&](const ImDrawVert& v) { return v.col == background; }));
    CHECK(w->WindowPadding.x == 0.0f);
    CHECK(w->WindowPadding.y == 0.0f);
    CHECK(w->WindowBorderSize == 0.0f);
}

}

TEST_CASE("a mini plot window has no frame of its own, floating or docked",
    "[mini_plot][docking][workspace]") {
    hedgr::testing::headless_imgui imgui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto rise = [](std::size_t k) {
        return 1.20 + static_cast<double>(k) * 0.001;
    };
    static_cast<void>(card_with(desk, 60, rise, "float"));
    static_cast<void>(card_with(desk, 60, rise, "left"));
    static_cast<void>(card_with(desk, 60, rise, "right"));
    const std::string settings = "[Window][###card-float]\nPos=700,60\nSize=240,170\n\n";
    ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());
    hedgr::ui::render::workspace workspace;
    auto input = desk.input();
    input.wall_now_ns = now_ns;

    for (int i = 0; i < 4; ++i) {
        ImGui::NewFrame();
        if (i == 0) {
            const ImGuiID root = ImGui::GetID("test-dock");
            ImGui::DockBuilderRemoveNode(root);
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodePos(root, ImVec2(20.0f, 20.0f));
            ImGui::DockBuilderSetNodeSize(root, ImVec2(600.0f, 300.0f));
            ImGuiID right = 0;
            const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
                0.5f, nullptr, &right);
            ImGui::DockBuilderDockWindow("###card-left", left);
            ImGui::DockBuilderDockWindow("###card-right", right);
            ImGui::DockBuilderFinish(root);
        }
        workspace.render(input);
        ImGui::Render();
    }

    REQUIRE(ImGui::FindWindowByName("###card-left")->DockIsActive);
    REQUIRE_FALSE(ImGui::FindWindowByName("###card-float")->DockIsActive);
    CHECK(ImGui::FindWindowByName("###card-float")->Size.x == 240.0f);
    CHECK(ImGui::FindWindowByName("###card-float")->Size.y == 170.0f);
    check_card_fills_window("###card-float");
    check_card_fills_window("###card-left");
    check_card_fills_window("###card-right");
}

/*
 * Hidden from the suite: it writes a picture of cards for a person to look at.
 * Run it by name with HEDGR_MINI_PLOT_PPM set.
 */
TEST_CASE("mini plot capture", "[.gallery]") {
    const char* out = std::getenv("HEDGR_MINI_PLOT_PPM");
    REQUIRE(out != nullptr);
    constexpr float width = 900.0f;
    constexpr float height = 520.0f;

    hedgr::testing::headless_imgui imgui;
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();
    hedgr::testing::desk desk;
    struct shape final {
        const char* id;
        double start;
        double slope;
        double wobble;
        ImVec2 pos;
        ImVec2 size;
    };
    /* The first five float. The last two are docked side by side. */
    const std::array<shape, 7> shapes{{
        {"a", 1.2000, 0.0006, 0.004, ImVec2(10, 10), ImVec2(220, 170)},
        {"b", 1.4000, -0.0007, 0.004, ImVec2(240, 10), ImVec2(220, 170)},
        {"c", 0.7000, 0.0003, 0.002, ImVec2(470, 10), ImVec2(160, 130)},
        {"d", 2400.0, 0.9, 6.0, ImVec2(640, 10), ImVec2(160, 130)},
        {"e", 1.1000, 0.0004, 0.003, ImVec2(10, 200), ImVec2(300, 250)},
        {"f", 1.2000, 0.0006, 0.004, ImVec2(0, 0), ImVec2(0, 0)},
        {"g", 1.4000, -0.0007, 0.004, ImVec2(0, 0), ImVec2(0, 0)}}};
    std::string settings;
    for (const auto& s : shapes) {
        static_cast<void>(card_with(desk, 200,
            [&](std::size_t k) {
                return s.start + s.slope * static_cast<double>(k)
                    + s.wobble * std::sin(static_cast<double>(k) * 0.4);
            },
            s.id));
        if (s.size.x > 0.0f)
            settings += std::format("[Window][###card-{}]\nPos={},{}\nSize={},{}\n\n",
                s.id, s.pos.x, s.pos.y, s.size.x, s.size.y);
    }
    ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());

    hedgr::ui::render::workspace workspace;
    auto input = desk.input();
    input.wall_now_ns = now_ns;
    hedgr::testing::software_raster raster(static_cast<int>(width),
        static_cast<int>(height));
    for (int pass = 0; pass < 4; ++pass) {
        ImGui::NewFrame();
        if (pass == 0) {
            const ImGuiID root = ImGui::GetID("test-dock");
            ImGui::DockBuilderRemoveNode(root);
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodePos(root, ImVec2(330.0f, 200.0f));
            ImGui::DockBuilderSetNodeSize(root, ImVec2(450.0f, 250.0f));
            ImGuiID right = 0;
            const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
                0.5f, nullptr, &right);
            ImGui::DockBuilderDockWindow("###card-f", left);
            ImGui::DockBuilderDockWindow("###card-g", right);
            ImGui::DockBuilderFinish(root);
        }
        workspace.render(input);
        ImGui::Render();
        raster.update_textures();
    }
    raster.draw(*ImGui::GetDrawData(),
        ImGui::ColorConvertFloat4ToU32(theme::base_surface()));
    CHECK(raster.write_ppm(std::filesystem::path(out)));
}
