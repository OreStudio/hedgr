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
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "software_raster.hpp"
#include <catch2/catch_all.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;

constexpr std::int64_t now_ns = 1'760'000'000'000'000'000;
constexpr std::int64_t step_ns = 600'000'000'000;

/** @brief A card whose series holds @p count points ending now, shaped by @p value. */
template<class Value>
model::state::window_handle card_with(hedgr::testing::desk& desk,
    std::size_t count, Value value, std::string id = "s") {
    const auto handle = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = id},
        "card-" + id);
    const auto series = desk.market.intern("i", "a", id);
    desk.windows.set_series(handle, std::span{&series, 1});
    for (std::size_t k = 0; k < count; ++k)
        desk.market.append(series, model::domain::market_level_for_span(
                                       render::mini_plot_span_ns),
            {.at_ns = now_ns
                    - static_cast<std::int64_t>(count - 1 - k) * step_ns,
                .value = value(k)},
            1);
    return handle;
}

/** @brief What a card put in its window's draw list. */
struct card_draw final {
    int line_vertices = 0;
    int fill_vertices = 0;
    int total_vertices = 0;
    ImVec4 window_clip;
};

[[nodiscard]] bool same_rgb(ImU32 a, ImU32 b) {
    return (a & 0x00FFFFFFu) == (b & 0x00FFFFFFu);
}

/** @brief Draws the card in a window of @p size and counts what it drew. */
card_draw draw_card(hedgr::testing::desk& desk,
    model::state::window_handle handle, theme::tone tone, ImVec2 size) {
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    render::window_ui_state state;
    auto input = desk.input();
    input.wall_now_ns = now_ns;
    card_draw result;
    const ImU32 colour = theme::pill_colours(tone).text;
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
        for (int i = 0; i < draw->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = draw->VtxBuffer[i];
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

TEST_CASE("a rising series draws its line and fill in the good tone",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.20 + static_cast<double>(k) * 0.001; });

    const auto good = draw_card(desk, handle, theme::tone::good,
        ImVec2(260.0f, 170.0f));
    const auto bad = draw_card(desk, handle, theme::tone::bad,
        ImVec2(260.0f, 170.0f));
    INFO("good line " << good.line_vertices << " fill " << good.fill_vertices);
    CHECK(good.line_vertices > 0);
    CHECK(good.fill_vertices > 0);
    CHECK(bad.line_vertices == 0);
    CHECK(bad.fill_vertices == 0);
}

TEST_CASE("a falling series draws its line and fill in the bad tone",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    theme::apply_theme();
    hedgr::testing::desk desk;
    const auto handle = card_with(desk, 60,
        [](std::size_t k) { return 1.30 - static_cast<double>(k) * 0.001; });

    const auto bad = draw_card(desk, handle, theme::tone::bad,
        ImVec2(260.0f, 170.0f));
    const auto good = draw_card(desk, handle, theme::tone::good,
        ImVec2(260.0f, 170.0f));
    CHECK(bad.line_vertices > 0);
    CHECK(bad.fill_vertices > 0);
    CHECK(good.line_vertices == 0);
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
    CHECK(render::mini_plot_first(points, render::mini_plot_span_ns) == 0);
    points.size = 1000;
    const auto first = render::mini_plot_first(points, 100 * step_ns);
    CHECK(first == 899);
}

/*
 * Hidden from the suite: it writes a picture of cards for a person to look at.
 * Run it by name with HEDGR_MINI_PLOT_PPM set.
 */
TEST_CASE("mini plot capture", "[.gallery]") {
    const char* out = std::getenv("HEDGR_MINI_PLOT_PPM");
    REQUIRE(out != nullptr);
    constexpr float width = 760.0f;
    constexpr float height = 420.0f;

    hedgr::testing::headless_imgui imgui;
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();
    hedgr::testing::desk desk;
    struct shape final {
        const char* id;
        double start;
        double slope;
        double wobble;
        ImVec2 size;
    };
    const std::array<shape, 5> shapes{{
        {"a", 1.2000, 0.0006, 0.004, ImVec2(220, 150)},
        {"b", 1.4000, -0.0007, 0.004, ImVec2(220, 150)},
        {"c", 0.7000, 0.0003, 0.002, ImVec2(160, 110)},
        {"d", 2400.0, 0.9, 6.0, ImVec2(160, 110)},
        {"e", 1.1000, 0.0004, 0.003, ImVec2(300, 220)}}};
    std::vector<model::state::window_handle> handles;
    for (const auto& s : shapes)
        handles.push_back(card_with(desk, 200,
            [&](std::size_t k) {
                return s.start + s.slope * static_cast<double>(k)
                    + s.wobble * std::sin(static_cast<double>(k) * 0.4);
            },
            s.id));

    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    hedgr::testing::software_raster raster(static_cast<int>(width),
        static_cast<int>(height));
    for (int pass = 0; pass < 3; ++pass) {
        auto input = desk.input();
        input.wall_now_ns = now_ns;
        ImGui::NewFrame();
        ImVec2 at(10.0f, 10.0f);
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            if (at.x + shapes[i].size.x > width) {
                at = ImVec2(10.0f, at.y + 240.0f);
            }
            render::window_ui_state state;
            ImGui::SetNextWindowPos(at);
            ImGui::SetNextWindowSize(shapes[i].size);
            ImGui::Begin(shapes[i].id, nullptr, ImGuiWindowFlags_NoDecoration);
            render::renderer_for(model::domain::window_kind::mini_plot)(
                handles[i], input, state, status, lock);
            ImGui::End();
            at.x += shapes[i].size.x + 10.0f;
        }
        ImGui::Render();
        raster.update_textures();
    }
    raster.draw(*ImGui::GetDrawData(),
        ImGui::ColorConvertFloat4ToU32(theme::base_surface()));
    CHECK(raster.write_ppm(std::filesystem::path(out)));
}
