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
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "software_raster.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;
using Catch::Approx;

constexpr std::int64_t half_second = 500'000'000;
constexpr std::int64_t series_start_ns = 1'760'000'000'000'000'000;
constexpr std::size_t chart_points = 120;
constexpr std::int64_t newest_at_ns = series_start_ns
    + static_cast<std::int64_t>(chart_points - 1) * half_second;

/** @brief A themed headless context, so the state colours are the real ones. */
struct themed final {
    headless_imgui imgui;
    themed() { theme::apply_theme(); }
};

/** @brief The vertices of the window the headless frame draws into. */
std::vector<ImDrawVert> frame_vertices() {
    const ImGuiWindow* window = ImGui::FindWindowByName("frame");
    REQUIRE(window != nullptr);
    const auto& buffer = window->DrawList->VtxBuffer;
    return {buffer.begin(), buffer.end()};
}

/** @brief The vertices of every list the last frame rendered. */
std::vector<ImDrawVert> all_vertices() {
    std::vector<ImDrawVert> result;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
        result.insert(result.end(), list->VtxBuffer.begin(),
            list->VtxBuffer.end());
    return result;
}

bool has_colour(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    return std::ranges::any_of(vertices,
        [&](const ImDrawVert& v) { return v.col == colour; });
}

/** @brief Whether any vertex wears a pill or strip colour of a state tone. */
bool has_state_tone(const std::vector<ImDrawVert>& vertices) {
    for (const auto tone :
        {theme::tone::good, theme::tone::bad, theme::tone::warning}) {
        const auto pill = theme::pill_colours(tone);
        if (has_colour(vertices, pill.fill) || has_colour(vertices, pill.border)
            || has_colour(vertices, pill.text))
            return true;
    }
    return false;
}

bool has_pill(const std::vector<ImDrawVert>& vertices, theme::tone tone) {
    return has_colour(vertices, theme::pill_colours(tone).border);
}

/**
 * @brief The width of the box every solid vertex of @p colour falls in.
 *
 * An antialiased fill keeps its solid vertices half a pixel inside each edge,
 * so this reads about a pixel short of the shape.
 */
float solid_width(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    float left = FLT_MAX;
    float right = -FLT_MAX;
    for (const auto& v : vertices) {
        if (v.col != colour || v.uv.x != white.x || v.uv.y != white.y)
            continue;
        left = std::min(left, v.pos.x);
        right = std::max(right, v.pos.x);
    }
    return right > left ? right - left : 0.0f;
}

/** @brief The common pieces every window renderer takes. */
struct window_fixture {
    themed context;
    desk desk;
    render::window_ui_state state;
    widget::status_bar status;
    render::lock_control lock;
};

/** @brief A chart over a series that moves by @p step a point. */
struct chart_fixture final : window_fixture {
    model::state::window_handle window;

    explicit chart_fixture(double step,
        const std::function<double(std::size_t)>& wobble = {}) {
        const auto series = desk.market.intern("i", "a", "s");
        for (std::size_t p = 0; p < chart_points; ++p) {
            const double value = 1.25 + step * static_cast<double>(p)
                + (wobble ? wobble(p) : 0.0);
            static_cast<void>(desk.market.append(series,
                model::domain::market_level::fine,
                model::state::series_point{
                    .at_ns = series_start_ns
                        + static_cast<std::int64_t>(p) * half_second,
                    .value = value},
                1));
        }
        window = desk.open(model::domain::window_kind::chart,
            model::domain::chart_state{.label = "FX GBP/USD",
                .series_id = "s"});
        desk.windows.set_series(window, std::span{&series, 1});
        state.market_view.chart_span = 3;
    }

    void draw(std::int64_t wall_now_ns) {
        auto input = desk.input();
        input.wall_now_ns = wall_now_ns;
        input.now_ns = wall_now_ns;
        hedgr::testing::frame([&] {
            render::render_chart(window, input, state, status, lock);
        });
    }
};

std::vector<model::domain::term_curve> curves(std::size_t count) {
    static const std::vector<std::string> tenors{"ON", "1W", "1M", "3M",
        "6M", "1Y", "2Y", "5Y", "10Y", "30Y"};
    std::vector<model::domain::term_curve> result;
    for (std::size_t c = 0; c < count; ++c) {
        model::domain::term_curve curve{
            .at_ns = newest_at_ns - static_cast<std::int64_t>(count - c)
                * 60'000'000'000};
        for (std::size_t t = 0; t < tenors.size(); ++t) {
            curve.nodes.push_back({.tenor = tenors[t],
                .value = 0.038 + 0.0004 * static_cast<double>(c)
                    + 0.003 * std::log1p(static_cast<double>(t)),
                .at_ns = curve.at_ns});
        }
        result.push_back(std::move(curve));
    }
    return result;
}

struct term_fixture final : window_fixture {
    model::state::window_handle window;

    term_fixture() {
        desk.term_structures.set("i", "a", "k", "IR GBP SONIA", curves(4));
        window = desk.open(model::domain::window_kind::term_structure,
            model::domain::term_structure_state{.label = "IR GBP SONIA",
                .curve_key = "k"});
        state.market_view.term_history = 4;
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        hedgr::testing::frame([&] {
            render::render_term_structure(window, input, state, status, lock);
        });
    }
};

/** @brief A CRM over five currencies whose cells take @p status_of's verdict. */
struct crm_fixture final : window_fixture {
    model::state::window_handle window;

    explicit crm_fixture(
        const std::function<std::string(std::size_t)>& status_of) {
        static const std::vector<std::string> codes{"EUR", "GBP", "JPY",
            "USD", "CHF"};
        std::vector<model::domain::crm_cell> cells;
        std::size_t at = 0;
        for (const auto& base : codes) {
            for (const auto& quote : codes) {
                if (base == quote)
                    continue;
                const double delta = (static_cast<double>(at % 7) - 3.0) * 0.01;
                cells.push_back(model::domain::crm_cell{.crm_name = "majors",
                    .base_currency_code = base, .quote_currency_code = quote,
                    .rate = 1.0 + static_cast<double>(at) / 37.0,
                    .status = status_of(at), .as_of = "2026-10-22T10:00:00Z",
                    .delta_pct = delta});
                ++at;
            }
        }
        desk.crm.set("i", "a", "p", "majors", false, std::move(cells), {},
            newest_at_ns, 4'000'000);
        window = desk.open(model::domain::window_kind::crm_matrix,
            model::domain::crm_matrix_state{.party_id = "p",
                .crm_name = "majors"});
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        hedgr::testing::frame([&] {
            render::render_crm_matrix(window, input, state, status, lock);
        });
    }
};

struct browser_fixture final : window_fixture {
    model::state::window_handle window;

    browser_fixture() {
        std::vector<model::domain::market_series_row> rows;
        const char* const classes[] = {"FX", "FX", "EQ", "CMDTY"};
        for (std::size_t i = 0; i < 12; ++i) {
            rows.push_back(model::domain::market_series_row{
                .id = std::format("s-{}", i),
                .uri = std::format("fx/gbpusd/{}", i),
                .label = std::format("FX GBP/USD {:02}", i),
                .asset_class = classes[i % 4],
                .producer_kind = "SYNTHETIC"});
        }
        desk.market.apply_catalogue("i", "a", std::move(rows));
        window = desk.open(model::domain::window_kind::series_browser,
            model::domain::series_browser_state{});
    }

    void draw() {
        auto input = desk.input();
        hedgr::testing::frame([&] {
            render::render_series_browser(window, input, state, status, lock);
        });
    }
};

struct tenants_fixture final : window_fixture {
    model::state::window_handle window;

    tenants_fixture() {
        model::domain::deployment_snapshot snapshot;
        snapshot.observed_at_ns = newest_at_ns;
        snapshot.tenant_total = 3;
        snapshot.tenants = {
            {.id = "t1", .name = "Northwind", .status = "active",
                .type = "production"},
            {.id = "t2", .name = "Contoso", .status = "suspended",
                .type = "evaluation"},
            {.id = "t3", .name = "Fabrikam", .status = "archived",
                .type = "production"}};
        desk.sessions.set_deployment(desk.session, std::move(snapshot));
        window = desk.open(model::domain::window_kind::command_centre,
            model::domain::command_centre_state{.instance_id = "i"});
        render::open_command_centre_screen_for_capture("tenants");
    }

    void draw() {
        auto input = desk.input();
        hedgr::testing::frame([&] {
            render::render_command_centre_window(window, input, state, status,
                lock);
        });
    }
};

/** @brief Draws @p draw a few times and returns the last frame's vertices. */
template<class Draw, class Read>
auto settled(Draw&& draw, Read&& read) {
    for (int i = 0; i < 3; ++i)
        draw();
    return read();
}

}

TEST_CASE("a rising live chart wears a good change pill and no bad one",
    "[look][chart]") {
    chart_fixture chart{0.0001};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK_FALSE(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a falling live chart wears a bad change pill and no good one",
    "[look][chart]") {
    chart_fixture chart{-0.0001};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    CHECK(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::good));
}

TEST_CASE("a delayed chart's change wears the delay, a stale one no state",
    "[look][chart]") {
    {
        chart_fixture delayed{0.0001};
        const auto late = settled(
            [&] { delayed.draw(newest_at_ns + 10'000'000'000); },
            frame_vertices);
        CHECK(has_pill(late, theme::tone::warning));
        CHECK_FALSE(has_pill(late, theme::tone::good));
    }
    {
        chart_fixture stale{0.0001};
        const auto old = settled(
            [&] { stale.draw(newest_at_ns + 120'000'000'000); },
            frame_vertices);
        CHECK_FALSE(has_state_tone(old));
        CHECK(has_pill(old, theme::tone::neutral));
    }
}

TEST_CASE("a chart names its series in a neutral pill", "[look][chart]") {
    chart_fixture chart{0.0};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    const ImU32 fill = theme::pill_colours(theme::tone::neutral).fill;
    CHECK(solid_width(vertices, fill)
        >= widget::value_pill_size("FX GBP/USD").x - 1.5f);
    CHECK_FALSE(has_state_tone(vertices));
}

TEST_CASE("a term structure names its curve in a pill and wears no state",
    "[look][rates]") {
    term_fixture terms;
    const auto vertices = settled([&] { terms.draw(); }, frame_vertices);
    const ImU32 fill = theme::pill_colours(theme::tone::neutral).fill;
    CHECK(solid_width(vertices, fill)
        == Approx(widget::value_pill_size("IR GBP SONIA").x).margin(1.5));
    CHECK_FALSE(has_state_tone(vertices));
}

TEST_CASE("a CRM of fresh cells counts them in a good pill", "[look][crm]") {
    crm_fixture crm{[](std::size_t) { return std::string{"fresh"}; }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
    CHECK_FALSE(has_pill(vertices, theme::tone::bad));
}

TEST_CASE("a CRM with stale cells counts them in a warning pill",
    "[look][crm]") {
    crm_fixture crm{[](std::size_t at) {
        return std::string{at % 4 == 0 ? "stale" : "fresh"};
    }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a CRM with no fresh cell claims no good state", "[look][crm]") {
    crm_fixture crm{[](std::size_t) { return std::string{"stale"}; }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK_FALSE(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a series browser heads its pickers with captions",
    "[look][browser]") {
    browser_fixture browser;
    const auto vertices = settled([&] { browser.draw(); }, frame_vertices);
    CHECK(has_colour(vertices, ImGui::GetColorU32(theme::dim_text())));
    CHECK_FALSE(has_state_tone(vertices));
}

TEST_CASE("a tenant's status is a pill in its state's tone",
    "[look][command-centre]") {
    tenants_fixture centre;
    const auto vertices = settled([&] { centre.draw(); }, all_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
}

/*
 * Hidden from the suite: it paints the market windows for a person to look at
 * and asserts nothing about them. Run it by name with HEDGR_WINDOW_GALLERY set
 * to a directory; it writes one PPM per window there.
 */
TEST_CASE("window gallery", "[.gallery]") {
    const char* out = std::getenv("HEDGR_WINDOW_GALLERY");
    REQUIRE(out != nullptr);
    const std::filesystem::path dir{out};

    const auto paint = [&](const char* name, auto& fixture, auto draw) {
        hedgr::ui::icon::load_ui_fonts(15.0f);
        ImGui::GetIO().DisplaySize = ImVec2(1000.0f, 700.0f);
        hedgr::testing::software_raster raster(1000, 700);
        for (int i = 0; i < 4; ++i) {
            draw(fixture);
            raster.update_textures();
        }
        raster.draw(*ImGui::GetDrawData(),
            ImGui::ColorConvertFloat4ToU32(
                ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)));
        CHECK(raster.write_ppm(dir / std::format("{}.ppm", name)));
    };

    {
        chart_fixture chart{0.00002, [](std::size_t p) {
            return 0.0006 * std::sin(static_cast<double>(p) / 7.0);
        }};
        paint("chart", chart, [](auto& f) { f.draw(newest_at_ns); });
    }
    {
        term_fixture terms;
        paint("term-structure", terms, [](auto& f) { f.draw(); });
    }
    {
        crm_fixture crm{[](std::size_t at) {
            return std::string{at % 6 == 0 ? "stale" : "fresh"};
        }};
        paint("crm", crm, [](auto& f) { f.draw(); });
    }
    {
        browser_fixture browser;
        paint("series-browser", browser, [](auto& f) { f.draw(); });
    }
    {
        tenants_fixture centre;
        paint("tenants", centre, [](auto& f) { f.draw(); });
    }
}
