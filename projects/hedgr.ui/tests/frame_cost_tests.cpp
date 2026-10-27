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
#include "allocation_counter.hpp"
#include "headless_desk.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/status_bar.hpp"
#include "hedgr.ui/render/toolbar.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
using hedgr::testing::desk;
using hedgr::testing::frame;
using hedgr::testing::headless_imgui;

/** @brief The texture asks of one series browser frame over @p rows series. */
std::size_t texture_asks_over(std::size_t rows) {
    headless_imgui imgui;
    desk desk;
    std::vector<model::domain::market_series_row> catalogue;
    for (std::size_t i = 0; i < rows; ++i) {
        catalogue.push_back(model::domain::market_series_row{
            .id = std::format("s-{}", i),
            .label = std::format("FX GBP/USD {:05}", i),
            .asset_class = "FX", .provider_code = "ore"});
    }
    desk.market.apply_catalogue("i", "a", std::move(catalogue));
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();

    /* The first frame lays the table out; the second is the steady state. */
    for (int i = 0; i < 2; ++i) {
        desk.textures.gets = 0;
        frame([&] {
            render::render_series_browser(handle, input, state, status, lock);
        });
    }
    return desk.textures.gets;
}

}

TEST_CASE("a series browser draws only the flags of the rows in view",
    "[frame][browser]") {
    const auto small = texture_asks_over(200);
    const auto large = texture_asks_over(5000);
    CHECK(small > 0);
    CHECK(large == small);
}

namespace {

/** @brief Allocations of one steady workspace frame with @p count collapsed charts. */
std::size_t allocations_with_collapsed(std::size_t count) {
    headless_imgui imgui;
    desk desk;
    std::string settings;
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = std::format("chart-window-{:04}", i);
        static_cast<void>(desk.open(model::domain::window_kind::chart,
            model::domain::chart_state{
                .label = "FX GBP/USD spot, synthetic feed", .series_id = "s"},
            id));
        settings += std::format("[Window][###{}]\nCollapsed=1\n\n", id);
    }
    ImGui::LoadIniSettingsFromMemory(settings.c_str(), settings.size());
    render::workspace workspace;
    auto input = desk.input();

    const auto draw = [&] {
        ImGui::NewFrame();
        workspace.render(input);
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    return hedgr::testing::allocations() - before;
}

}

TEST_CASE("a collapsed window costs a frame no allocation", "[frame][workspace]") {
    const auto few = allocations_with_collapsed(10);
    const auto many = allocations_with_collapsed(110);
    INFO("allocations with 10 collapsed: " << few << ", with 110: " << many);
    CHECK(many == few);
}

namespace {

const std::vector<std::string> crm_currencies{"AUD", "CAD", "CHF", "EUR",
    "GBP", "JPY", "NOK", "NZD", "SEK", "SGD", "USD", "ZAR"};

/** @brief A desk holding one CRM batch over every pair of @p count currencies. */
void hold_crm(desk& desk, std::size_t count) {
    std::vector<model::domain::crm_cell> cells;
    for (std::size_t b = 0; b < count; ++b) {
        for (std::size_t q = 0; q < count; ++q) {
            if (b == q)
                continue;
            cells.push_back(model::domain::crm_cell{.crm_name = "majors",
                .base_currency_code = crm_currencies[b],
                .quote_currency_code = crm_currencies[q],
                .rate = 1.0 + static_cast<double>(b * count + q) / 1000.0,
                .status = "fresh", .as_of = "2026-10-19T10:00:00Z",
                .delta_pct = 0.01});
        }
    }
    desk.crm.set("i", "a", "p", "majors", false, std::move(cells), {}, 1, 0);
}

/** @brief Allocations of one steady CRM frame over @p count currencies. */
std::size_t crm_frame_allocations(std::size_t count) {
    headless_imgui imgui;
    desk desk;
    hold_crm(desk, count);
    const auto handle = desk.open(model::domain::window_kind::crm_matrix,
        model::domain::crm_matrix_state{.party_id = "p", .crm_name = "majors"});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_crm_matrix(handle, input, state, status, lock);
        });
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    return hedgr::testing::allocations() - before;
}

}

TEST_CASE("a steady CRM frame costs nothing per cell", "[frame][crm]") {
    const auto small = crm_frame_allocations(3);
    const auto large = crm_frame_allocations(12);
    INFO("allocations over 3 currencies: " << small << ", over 12: " << large);
    CHECK(large == small);
}

TEST_CASE("drawing a CRM window never asks for a read", "[frame][crm]") {
    headless_imgui imgui;
    desk desk;
    hold_crm(desk, 3);
    const auto handle = desk.open(model::domain::window_kind::crm_matrix,
        model::domain::crm_matrix_state{.party_id = "p", .crm_name = "majors"});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();

    /* Twelve frames a second apart: well past any polling cadence. */
    ImGui::GetIO().DeltaTime = 1.0f;
    for (int i = 0; i < 12; ++i)
        frame([&] {
            render::render_crm_matrix(handle, input, state, status, lock);
        });
    for (const auto& request : desk.commands.drain())
        CHECK(request.kind != model::command::command_kind::refresh_window);
}

TEST_CASE("a CRM asks for the flag of any currency the tenant has an image for",
    "[frame][crm]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"THB", "img-th"}, {"USD", "img-us"}});
    desk.crm.set("i", "a", "p", "majors", false,
        {model::domain::crm_cell{.crm_name = "majors",
            .base_currency_code = "THB", .quote_currency_code = "USD",
            .rate = 0.03, .status = "fresh"}},
        {}, 1, 0);
    const auto handle = desk.open(model::domain::window_kind::crm_matrix,
        model::domain::crm_matrix_state{.party_id = "p", .crm_name = "majors"});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    frame([&] {
        render::render_crm_matrix(handle, input, state, status, lock);
    });

    std::vector<std::string> asked;
    for (const auto& request : desk.commands.drain())
        if (request.kind == model::command::command_kind::load_image)
            asked.push_back(request.image_id);
    CHECK(asked == std::vector<std::string>{"img-th"});
}

namespace {

/** @brief What drawing one label's flags did to the window. */
struct flag_draw final {
    int vertices = 0;
    float cursor_moved = 0.0f;
    std::size_t texture_gets = 0;
    std::vector<std::string> asked;
};

flag_draw draw_flags_for(desk& desk, std::string_view label) {
    flag_draw result;
    auto input = desk.input();
    const auto gets_before = desk.textures.gets;
    frame([&] {
        const auto* list = ImGui::GetWindowDrawList();
        const auto vertices_before = list->VtxBuffer.Size;
        const auto cursor_before = ImGui::GetCursorPosY();
        render::draw_series_flags(input, desk.session, label);
        result.vertices = list->VtxBuffer.Size - vertices_before;
        result.cursor_moved = ImGui::GetCursorPosY() - cursor_before;
    });
    result.texture_gets = desk.textures.gets - gets_before;
    for (const auto& request : desk.commands.drain())
        if (request.kind == model::command::command_kind::load_image)
            result.asked.push_back(request.image_id);
    return result;
}

}

TEST_CASE("a currency is drawn from its server image once it loads",
    "[frame][flags]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"TWD", "img-tw"}});

    const auto before = draw_flags_for(desk, "IR TWD IR_SWAP");
    CHECK(before.asked == std::vector<std::string>{"img-tw"});
    CHECK(before.vertices == 0);

    static_cast<void>(desk.images.put("t", "img-tw", {.mime_type = "image/svg+xml",
        .data = std::vector<std::uint8_t>(4, 3)}));
    const auto after = draw_flags_for(desk, "IR TWD IR_SWAP");
    CHECK(after.asked.empty());
    CHECK(after.texture_gets == 1);
    CHECK(after.vertices > 0);
    CHECK(after.cursor_moved > 0.0f);
}

TEST_CASE("a currency with no server image draws nothing and reserves no space",
    "[frame][flags]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"GBP", "img-gb"}});

    const auto drawn = draw_flags_for(desk, "IR TWD IR_SWAP");

    CHECK(drawn.asked.empty());
    CHECK(drawn.texture_gets == 0);
    CHECK(drawn.vertices == 0);
    CHECK(drawn.cursor_moved == 0.0f);
}

namespace {

constexpr std::int64_t half_second = 500'000'000;
constexpr std::int64_t series_start_ns = 1'760'000'000'000'000'000;
/** @brief The fine ring's capacity, every point of which is in view at one hour. */
constexpr std::size_t ring_points = model::domain::market_fine_capacity;

/**
 * @brief Charts over series that each hold a full fine ring.
 *
 * The points are half a second apart, so the one hour span puts every point of
 * the ring in view, the widest the chart can be asked to plot.
 */
struct chart_desk final {
    headless_imgui imgui;
    desk desk;
    std::vector<model::state::window_handle> windows;
    std::vector<model::state::series_handle> series;
    std::vector<std::string> names;
    std::vector<std::unique_ptr<render::window_ui_state>> states;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    std::int64_t next_at_ns = series_start_ns;

    /** @brief Whether the charts are docked, two to a dock node. */
    bool docked = false;
    bool docks_built = false;

    explicit chart_desk(std::size_t count, bool docked_charts = false)
        : docked(docked_charts) {
        if (docked)
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        for (std::size_t i = 0; i < count; ++i) {
            const auto id = std::format("s-{}", i);
            const auto handle = desk.market.intern("i", "a", id);
            for (std::size_t p = 0; p < ring_points; ++p)
                append(handle, p);
            next_at_ns = series_start_ns
                + static_cast<std::int64_t>(ring_points) * half_second;
            const auto window = desk.open(model::domain::window_kind::chart,
                model::domain::chart_state{.label = "FX GBP/USD",
                    .series_id = id},
                std::format("chart-{}", i));
            desk.windows.set_series(window, std::span{&handle, 1});
            series.push_back(handle);
            names.push_back(std::format("chart-window-{}", i));
            windows.push_back(window);
            states.push_back(std::make_unique<render::window_ui_state>());
            states.back()->market_view.chart_span = 3;
        }
    }

    void append(model::state::series_handle handle, std::size_t index) {
        const auto at = series_start_ns
            + static_cast<std::int64_t>(index) * half_second;
        const auto value = 1.0 + static_cast<double>(index % 97) / 1000.0;
        static_cast<void>(desk.market.append(handle, model::domain::market_level::fine,
            model::state::series_point{.at_ns = at, .value = value},
            1));
    }

    /** @brief Appends the next bucket to every series. */
    void tick() {
        for (const auto handle : series) {
            const auto at = next_at_ns;
            const auto value = 1.0 + static_cast<double>(at % 89) / 1000.0;
            static_cast<void>(desk.market.append(handle,
                model::domain::market_level::fine,
                model::state::series_point{.at_ns = at, .value = value}, 1));
        }
        next_at_ns += half_second;
    }

    /** @brief Draws every chart once and returns the vertices submitted. */
    int draw() {
        auto input = desk.input();
        ImGui::NewFrame();
        if (docked && !docks_built) {
            dock_in_pairs();
            docks_built = true;
        }
        for (std::size_t i = 0; i < windows.size(); ++i) {
            /* A window given a position every frame would leave its dock. */
            if (!docked) {
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f));
            }
            ImGui::Begin(names[i].c_str());
            render::render_chart(windows[i], input, *states[i], status, lock);
            ImGui::End();
        }
        ImGui::Render();
        return ImGui::GetDrawData()->TotalVtxCount;
    }

    /** @brief Splits one dock node per two charts, a chart in each half. */
    void dock_in_pairs() {
        for (std::size_t i = 0; i < names.size(); i += 2) {
            const ImGuiID root = ImGui::GetID(std::format("dock-{}", i).c_str());
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodePos(root, ImVec2(0.0f, 0.0f));
            ImGui::DockBuilderSetNodeSize(root, ImVec2(1800.0f, 500.0f));
            ImGuiID right = 0;
            const ImGuiID left = ImGui::DockBuilderSplitNode(root,
                ImGuiDir_Left, 0.5f, nullptr, &right);
            ImGui::DockBuilderDockWindow(names[i].c_str(), left);
            if (i + 1 < names.size())
                ImGui::DockBuilderDockWindow(names[i + 1].c_str(), right);
            ImGui::DockBuilderFinish(root);
        }
    }
};

/** @brief Allocations of one steady frame with @p count charts over full rings. */
std::size_t allocations_with_charts(std::size_t count, bool docked = false) {
    chart_desk charts{count, docked};
    int vertices = 0;
    for (int i = 0; i < 3; ++i)
        vertices = charts.draw();
    const auto before = hedgr::testing::allocations();
    const auto steady = charts.draw();
    const auto allocations = hedgr::testing::allocations() - before;

    /* The work happened: every chart plotted its whole ring, filled and lined. */
    CHECK(vertices > 0);
    CHECK(steady == vertices);
    CHECK(static_cast<std::size_t>(steady) >= count * (ring_points - 1) * 6);
    for (std::size_t i = 0; i < count; ++i) {
        const ImGuiWindow* window = ImGui::FindWindowByName(
            charts.names[i].c_str());
        REQUIRE(window != nullptr);
        CHECK(window->DockIsActive == docked);
        CHECK(charts.states[i]->plot.key.version
            == charts.desk.market.version(charts.series[i]));
        CHECK(charts.states[i]->plot.key.version != 0);
    }
    return allocations;
}

}

/*
 * A chart's chrome formats a few strings a frame, so the count is not zero. It
 * must be the same for every chart and nothing a chart's points add to it.
 */
TEST_CASE("a steady chart frame allocates the same for every chart",
    "[frame][chart]") {
    const auto few = allocations_with_charts(10);
    const auto many = allocations_with_charts(110);
    INFO("allocations with 10 charts: " << few << ", with 110: " << many);
    CHECK(few % 10 == 0);
    CHECK(many == few / 10 * 110);
    WARN("steady allocations per chart: " << few / 10);
}

TEST_CASE("a steady docked chart frame costs the same as a floating one",
    "[frame][chart][docking]") {
    const auto few = allocations_with_charts(10, true);
    const auto many = allocations_with_charts(110, true);
    INFO("allocations with 10 docked charts: " << few << ", with 110: " << many);
    CHECK(few % 10 == 0);
    CHECK(many == few / 10 * 110);
    CHECK(few == allocations_with_charts(10));
}

TEST_CASE("a frame after every series ticked allocates the same for every chart",
    "[frame][chart]") {
    const auto after_tick = [](std::size_t count) {
        chart_desk charts{count};
        for (int i = 0; i < 3; ++i)
            charts.draw();
        charts.tick();
        std::vector<std::uint64_t> before;
        for (const auto& state : charts.states)
            before.push_back(state->plot.key.version);
        const auto allocations_before = hedgr::testing::allocations();
        charts.draw();
        const auto allocations = hedgr::testing::allocations() - allocations_before;
        for (std::size_t i = 0; i < count; ++i) {
            CHECK(charts.states[i]->plot.key.version > before[i]);
            CHECK(charts.states[i]->plot.key.version
                == charts.desk.market.version(charts.series[i]));
        }
        return allocations;
    };
    const auto few = after_tick(10);
    const auto many = after_tick(110);
    INFO("allocations after a tick, 10 charts: " << few << ", 110: " << many);
    CHECK(few % 10 == 0);
    CHECK(many == few / 10 * 110);
    WARN("allocations per chart after a tick: " << few / 10);
}

TEST_CASE("a chart whose series is gone draws the reading state, not stale data",
    "[frame][chart]") {
    chart_desk charts{1};
    int vertices = 0;
    for (int i = 0; i < 3; ++i)
        vertices = charts.draw();
    REQUIRE(vertices > static_cast<int>(ring_points) * 6);
    REQUIRE(charts.states[0]->plot.key.version != 0);

    /* The window still holds the handle. The row it named is gone. */
    charts.desk.market.erase(charts.series[0]);
    REQUIRE(charts.desk.windows.series(charts.windows[0]).size() == 1);
    const auto after = charts.draw();
    CHECK(after < static_cast<int>(ring_points));
    CHECK(charts.states[0]->plot.key.version == 0);
    CHECK(charts.states[0]->plot.candles.empty());
    CHECK(charts.states[0]->plot.ema.empty());

    /* Interning the series again gives a new handle, which holds no points. */
    const auto fresh = charts.desk.market.intern("i", "a", "s-0");
    CHECK(fresh != charts.series[0]);
    CHECK(charts.draw() < static_cast<int>(ring_points));
}

TEST_CASE("the cost of a frame over 200 charts of a full ring",
    "[!benchmark][frame][chart]") {
    chart_desk charts{200};
    for (int i = 0; i < 3; ++i)
        charts.draw();

    const auto median_ms = [](auto&& run) {
        std::vector<double> samples;
        for (int i = 0; i < 15; ++i) {
            const auto start = std::chrono::steady_clock::now();
            run();
            samples.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
        std::ranges::sort(samples);
        return samples[samples.size() / 2];
    };

    const auto steady = median_ms([&] { CHECK(charts.draw() > 0); });
    const auto ticked = median_ms([&] {
        charts.tick();
        for (std::size_t i = 0; i < charts.states.size(); ++i)
            CHECK(charts.states[i]->plot.key.version
                != charts.desk.market.version(charts.series[i]));
        CHECK(charts.draw() > 0);
        for (std::size_t i = 0; i < charts.states.size(); ++i)
            CHECK(charts.states[i]->plot.key.version
                == charts.desk.market.version(charts.series[i]));
    });
    WARN("median ms of one frame over 200 charts, steady: " << steady);
    WARN("median ms of one frame over 200 charts, every series ticked: " << ticked);
}

namespace {

const std::vector<std::string> ladder_tenors{"10Y", "2Y", "1M", "6M", "ON",
    "3M", "1Y", "5Y", "1W", "30Y", "15Y", "2W"};

/** @brief @p count curves over the ladder tenors, each a little higher. */
std::vector<model::domain::term_curve> curves_of(std::size_t count) {
    std::vector<model::domain::term_curve> curves;
    for (std::size_t c = 0; c < count; ++c) {
        model::domain::term_curve curve{
            .at_ns = static_cast<std::int64_t>(c + 1) * 1'000'000'000};
        for (std::size_t t = 0; t < ladder_tenors.size(); ++t) {
            curve.nodes.push_back({.tenor = ladder_tenors[t],
                .value = 0.03 + 0.001 * static_cast<double>(c)
                    + 0.0001 * static_cast<double>(t),
                .at_ns = curve.at_ns});
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

/** @brief Allocations of one steady term structure frame overlaying @p overlays. */
std::size_t term_structure_frame_allocations(int overlays) {
    headless_imgui imgui;
    desk desk;
    desk.term_structures.set("i", "a", "k", "IR GBP", curves_of(10));
    const auto handle = desk.open(model::domain::window_kind::term_structure,
        model::domain::term_structure_state{.label = "IR GBP", .curve_key = "k"});
    render::window_ui_state state;
    state.market_view.term_history = overlays;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_term_structure(handle, input, state, status, lock);
        });
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    return hedgr::testing::allocations() - before;
}

}

TEST_CASE("a steady term structure frame costs nothing per overlay",
    "[frame][rates]") {
    const auto one = term_structure_frame_allocations(1);
    const auto ten = term_structure_frame_allocations(10);
    INFO("allocations with 1 overlay: " << one << ", with 10: " << ten);
    CHECK(ten == one);
}

namespace {

/** @brief A catalogue of @p rows FX series, a few of them on another party. */
std::vector<model::domain::market_series_row> browser_catalogue(
    std::size_t rows) {
    std::vector<model::domain::market_series_row> catalogue;
    for (std::size_t i = 0; i < rows; ++i) {
        catalogue.push_back(model::domain::market_series_row{
            .id = std::format("s-{}", i),
            .uri = std::format("fx/gbpusd/{}", i),
            .label = std::format("FX GBP/USD {:05}", i),
            .asset_class = i % 7 == 0 ? "RATES" : "FX",
            .provider_code = "ore",
            .party_id = i % 5 == 0 ? "p2" : "p1",
            .tenor = i % 3 == 0 ? "1M" : "1Y"});
    }
    return catalogue;
}

/** @brief Allocations of one steady series browser frame over @p rows rows. */
std::size_t browser_frame_allocations(std::size_t rows) {
    headless_imgui imgui;
    desk desk;
    desk.sessions.set_parties(desk.session,
        "p1\x1f" "First party\x1ep2\x1f" "Second party");
    desk.market.apply_catalogue("i", "a", browser_catalogue(rows));
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    state.market_party_id = "p1";
    state.filter[0] = 'u';
    state.filter[1] = 's';
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_series_browser(handle, input, state, status, lock);
        });
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    return hedgr::testing::allocations() - before;
}

}

TEST_CASE("a steady series browser frame costs nothing per row",
    "[frame][browser]") {
    const auto small = browser_frame_allocations(200);
    const auto large = browser_frame_allocations(5000);
    INFO("allocations over 200 rows: " << small << ", over 5000: " << large);
    CHECK(large == small);
    /* Equal counts pass on the old code too, whose rows cost bytes and time. The
       three left are the chrome's identity and footer text and the foot's
       one-combo vector, not the rows. */
    CHECK(small <= 3);
}

TEST_CASE("a series browser survives its catalogue being replaced by a shorter one",
    "[frame][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.market.apply_catalogue("i", "a", browser_catalogue(5000));
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_series_browser(handle, input, state, status, lock);
        });
    };
    draw();
    draw();
    desk.market.apply_catalogue("i", "a", browser_catalogue(3));
    draw();
    desk.textures.gets = 0;
    draw();
    CHECK(desk.textures.gets <= 3 * 2);
}

TEST_CASE("one steady series browser frame over 5000 rows",
    "[!benchmark][frame][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.market.apply_catalogue("i", "a", browser_catalogue(5000));
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    state.filter[0] = 'u';
    state.filter[1] = 's';
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_series_browser(handle, input, state, status, lock);
        });
    };
    for (int i = 0; i < 3; ++i)
        draw();
    BENCHMARK("steady frame") {
        draw();
    };
}

namespace {

/** @brief What one frame of the market browser drew for a single listed row. */
struct listed_row final {
    std::size_t flag_draws = 0;
    float text_x = 0.0f;
    std::vector<std::string> asked;
};

/**
 * @brief Draws the browser over one series of @p asset_class labelled @p label.
 *
 * A flag is a draw command textured by the desk's texture, and the font draws
 * every other glyph, so the two are told apart by texture. The row's text
 * starts at the leftmost glyph below the table header, in the first column.
 * A glyph is a vertex in the text colour that does not sample the font's white
 * pixel, which rectangles and the built-in flags do.
 */
listed_row draw_listed_row(desk& desk, std::string_view asset_class,
    std::string_view label) {
    listed_row result;
    desk.market.apply_catalogue("i", "a",
        {model::domain::market_series_row{.id = "s-0", .uri = "oresmd://s-0",
            .label = std::string{label}, .asset_class = std::string{asset_class},
            .provider_code = "ore", .curve_key = "s-0",
            .curve_label = std::string{label}}});
    const auto handle = desk.open(model::domain::window_kind::series_browser,
        model::domain::series_browser_state{});
    render::window_ui_state state;
    state.market_asset_class = std::string{asset_class};
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();

    /* The first frame lays the table out; the second is the steady state. */
    for (int i = 0; i < 2; ++i) {
        result.asked.clear();
        frame([&] {
            render::render_series_browser(handle, input, state, status, lock);
        });
        for (const auto& request : desk.commands.drain())
            if (request.kind == model::command::command_kind::load_image)
                result.asked.push_back(request.image_id);
    }

    const auto flag = static_cast<ImTextureID>(1);
    const auto white = ImGui::GetFontTexUvWhitePixel();
    const auto text = ImGui::GetColorU32(ImGuiCol_Text);
    result.flag_draws = 0;
    result.text_x = std::numeric_limits<float>::max();
    const auto* data = ImGui::GetDrawData();
    for (const auto* list : data->CmdLists) {
        const std::string owner = list->_OwnerName;
        if (owner.find("##curves") == std::string::npos
            && owner.find("##series") == std::string::npos)
            continue;
        std::vector<ImVec2> glyphs;
        for (const auto& cmd : list->CmdBuffer) {
            if (cmd.GetTexID() == flag) {
                ++result.flag_draws;
                continue;
            }
            for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
                const auto& vertex = list->VtxBuffer[
                    list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]];
                if ((vertex.uv.x != white.x || vertex.uv.y != white.y)
                    && vertex.col == text)
                    glyphs.push_back(vertex.pos);
            }
        }
        float top = std::numeric_limits<float>::max();
        for (const auto& p : glyphs)
            top = std::min(top, p.y);
        for (const auto& p : glyphs)
            if (p.y >= top + ImGui::GetTextLineHeight() * 0.9f && p.x < 200.0f)
                result.text_x = std::min(result.text_x, p.x);
    }
    return result;
}

}

TEST_CASE("an IR curve row draws the flag of its currency",
    "[frame][flags][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"TWD", "img-tw"}});
    static_cast<void>(desk.images.put("t", "img-tw", {.mime_type = "image/svg+xml",
        .data = std::vector<std::uint8_t>(4, 3)}));

    const auto drawn = draw_listed_row(desk, "IR", "IR TWD IR_SWAP");

    CHECK(drawn.flag_draws == 1);
    CHECK(drawn.asked.empty());
    CHECK(desk.textures.gets > 0);
}

TEST_CASE("an IR curve row asks for the image of a currency it has not loaded",
    "[frame][flags][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"TWD", "img-tw"}});

    const auto drawn = draw_listed_row(desk, "IR", "IR TWD IR_SWAP");

    CHECK(drawn.asked == std::vector<std::string>{"img-tw"});
    CHECK(drawn.flag_draws == 0);
}

TEST_CASE("an IR curve row with no server image shows no flag and keeps the text indent",
    "[frame][flags][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {});

    const auto drawn = draw_listed_row(desk, "IR", "IR HUF IR_SWAP");
    const auto bare = draw_listed_row(desk, "IR", "IR ZZZ IR_SWAP");

    CHECK(drawn.asked.empty());
    CHECK(drawn.flag_draws == 0);
    CHECK(desk.textures.gets == 0);
    CHECK(drawn.text_x == bare.text_x);
}

TEST_CASE("an IR curve row with no flag keeps the series row's text indent",
    "[frame][flags][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {});

    const auto curve = draw_listed_row(desk, "IR", "IR ZZZ IR_SWAP");
    /* The same first glyph, so the bearing cancels out of the comparison. */
    const auto series = draw_listed_row(desk, "FX", "IR ZZZ/QQQ");

    CHECK(curve.asked.empty());
    CHECK(curve.flag_draws == 0);
    CHECK(curve.text_x < 200.0f);
    CHECK(curve.text_x == series.text_x);
}

namespace {

/** @brief Allocations of one steady frame with @p count mini plot cards. */
std::size_t allocations_with_cards(std::size_t count, int& vertices_out) {
    headless_imgui imgui;
    desk desk;
    std::vector<model::state::window_handle> windows;
    std::vector<std::string> names;
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = std::format("s-{}", i);
        const auto series = desk.market.intern("i", "a", id);
        for (std::size_t p = 0; p < 1500; ++p)
            static_cast<void>(desk.market.append(series,
                model::domain::market_level_for_span(
                    24LL * 3600 * 1'000'000'000),
                model::state::series_point{
                    .at_ns = series_start_ns
                        + static_cast<std::int64_t>(p) * 60'000'000'000,
                    .value = 1.0 + static_cast<double>(p % 97) / 1000.0},
                1));
        const auto window = desk.open(model::domain::window_kind::mini_plot,
            model::domain::mini_plot_state{.label = "FX GBP/USD",
                .series_id = id, .span_ns = 24LL * 3600 * 1'000'000'000},
            std::format("card-{}", i));
        desk.windows.set_series(window, std::span{&series, 1});
        windows.push_back(window);
        names.push_back(std::format("card-window-{}", i));
    }
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    input.wall_now_ns = series_start_ns + 1500 * 60'000'000'000;
    const auto draw = [&] {
        ImGui::NewFrame();
        for (std::size_t i = 0; i < count; ++i) {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 150.0f));
            ImGui::Begin(names[i].c_str());
            render::render_mini_plot(windows[i], input, state, status, lock);
            ImGui::End();
        }
        ImGui::Render();
        return ImGui::GetDrawData()->TotalVtxCount;
    };
    for (int i = 0; i < 3; ++i)
        vertices_out = draw();
    const auto before = hedgr::testing::allocations();
    const auto steady = draw();
    const auto allocations = hedgr::testing::allocations() - before;
    CHECK(steady == vertices_out);
    return allocations;
}

}

TEST_CASE("a steady mini plot card frame allocates nothing", "[frame][mini_plot]") {
    int few_vertices = 0;
    int many_vertices = 0;
    const auto few = allocations_with_cards(10, few_vertices);
    const auto many = allocations_with_cards(60, many_vertices);
    INFO("allocations with 10 cards: " << few << ", with 60: " << many);
    CHECK(static_cast<std::size_t>(few_vertices) >= 10 * 95 * 6);
    CHECK(static_cast<std::size_t>(many_vertices) >= 60 * 95 * 6);
    CHECK(few == 0);
    CHECK(many == 0);
}

namespace {

/** @brief Allocations of one steady frame of @p draw, after warming up. */
template<class Draw>
std::size_t steady_allocations(Draw draw) {
    for (int i = 0; i < 3; ++i)
        draw();
    const auto before = hedgr::testing::allocations();
    draw();
    return hedgr::testing::allocations() - before;
}

constexpr model::domain::session_status states[] = {
    model::domain::session_status::disconnected,
    model::domain::session_status::failed,
    model::domain::session_status::connecting,
};

}

TEST_CASE("a view state costs a steady frame no allocation, each of its states",
    "[frame][view_state]") {
    headless_imgui imgui;
    using hedgr::ui::widget::view_state;
    using hedgr::ui::widget::view_verdict;
    using hedgr::ui::widget::view_words;
    const std::string_view reason = "certificate rejected";
    for (const auto verdict : {
             view_verdict{view_state::not_connected, view_words::not_connected,
                 {}},
             view_verdict{view_state::error, view_words::connection_failed,
                 reason},
             view_verdict{view_state::no_data, view_words::empty, reason},
             view_verdict{view_state::reading, view_words::none, {}}}) {
        const auto allocations = steady_allocations([&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
            ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
            hedgr::ui::widget::draw_view_state(verdict, ImVec2(0.0f, 0.0f),
                ImVec2(600.0f, 400.0f), 44.0f);
            ImGui::End();
            ImGui::Render();
        });
        CHECK(allocations == 0);
    }
}

TEST_CASE("a steady mini plot card frame allocates nothing in each connection state",
    "[frame][mini_plot][view_state]") {
    for (const auto status : states) {
        headless_imgui imgui;
        desk desk;
        desk.sessions.set_status(desk.session, status);
        const auto handle = desk.open(model::domain::window_kind::mini_plot,
            model::domain::mini_plot_state{.label = "FX GBP/USD",
                .series_id = "s"});
        const auto series = desk.market.intern("i", "a", "s");
        desk.windows.set_series(handle, std::span{&series, 1});
        render::window_ui_state state;
        hedgr::ui::widget::status_bar bar;
        render::lock_control lock;
        auto input = desk.input();
        const auto allocations = steady_allocations([&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 150.0f));
            ImGui::Begin("card");
            render::render_mini_plot(handle, input, state, bar, lock);
            ImGui::End();
            ImGui::Render();
        });
        INFO("session status " << static_cast<int>(status));
        CHECK(allocations == 0);
    }
}

TEST_CASE("a chart in each connection state allocates no more than a chart with data",
    "[frame][chart][view_state]") {
    const auto with_data = allocations_with_charts(1);
    for (const auto status : states) {
        headless_imgui imgui;
        desk desk;
        desk.sessions.set_status(desk.session, status);
        const auto handle = desk.open(model::domain::window_kind::chart,
            model::domain::chart_state{.label = "FX GBP/USD",
                .series_id = "s"});
        const auto series = desk.market.intern("i", "a", "s");
        desk.windows.set_series(handle, std::span{&series, 1});
        render::window_ui_state state;
        hedgr::ui::widget::status_bar bar;
        render::lock_control lock;
        auto input = desk.input();
        const auto allocations = steady_allocations([&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f));
            ImGui::Begin("chart");
            render::render_chart(handle, input, state, bar, lock);
            ImGui::End();
            ImGui::Render();
        });
        INFO("session status " << static_cast<int>(status));
        CHECK(allocations <= with_data);
    }

    /* The same for a connected chart that is still reading its series. */
    headless_imgui imgui;
    desk desk;
    const auto handle = desk.open(model::domain::window_kind::chart,
        model::domain::chart_state{.label = "FX GBP/USD", .series_id = "s"});
    const auto series = desk.market.intern("i", "a", "s");
    desk.windows.set_series(handle, std::span{&series, 1});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar bar;
    render::lock_control lock;
    auto input = desk.input();
    CHECK(steady_allocations([&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 500.0f));
        ImGui::Begin("chart");
        render::render_chart(handle, input, state, bar, lock);
        ImGui::End();
        ImGui::Render();
    }) <= with_data);
}

TEST_CASE("an open menu with every kind of row costs a steady frame no allocation",
    "[frame][menu]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    namespace widget = hedgr::ui::widget;
    bool opened = false;
    bool rows_drawn = false;
    const auto allocations = steady_allocations([&] {
        ImGui::NewFrame();
        if (!opened) {
            ImGui::OpenPopup("menu");
            opened = true;
        }
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 100.0f));
        ImGui::Begin("bar", nullptr, ImGuiWindowFlags_NoDecoration);
        static_cast<void>(widget::menu_label("File", true));
        ImGui::End();
        {
            const widget::menu_popup popup("menu", 0.0f, 60.0f);
            if (popup) {
                widget::menu_heading("Windows");
                static_cast<void>(widget::menu_item_row({.label = "Open"}));
                static_cast<void>(widget::menu_item_row(
                    {.label = "Pick", .icon = "i", .selected = true}));
                static_cast<void>(widget::menu_item_row(
                    {.label = "Locked", .enabled = false}));
                widget::menu_separator();
                static_cast<void>(widget::menu_item_row(
                    {.label = "More", .submenu = true}));
                static_cast<void>(widget::menu_item_row(
                    {.label = "Save", .shortcut = "Ctrl+S"}));
            }
            rows_drawn = static_cast<bool>(popup);
        }
        ImGui::Render();
    });
    CHECK(rows_drawn);
    CHECK(allocations == 0);
}

TEST_CASE("the top bar costs a steady frame no allocation, closed and with each menu open",
    "[frame][menu_bar]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    desk desk;
    auto input = desk.input();
    hedgr::ui::widget::menu_bar_state state;
    const std::string svg = "<svg xmlns='http://www.w3.org/2000/svg'/>";
    const auto draw = [&] {
        hedgr::ui::render::menu_bar_result result;
        ImGui::NewFrame();
        hedgr::ui::render::draw_menu_bar(input, svg, true, state, result);
        ImGui::Render();
    };
    CHECK(steady_allocations(draw) == 0);

    /* Sweep the pointer along the bar, to learn where each label is. */
    const ImGuiWindow* bar = ImGui::FindWindowByName("###hedgr-menu-bar");
    REQUIRE(bar != nullptr);
    const char* const names[] = {"File", "View", "Instances", "Accounts",
        "Tools", "Help"};
    float at[6] = {};
    for (float x = 2.0f; x < 800.0f; x += 2.0f) {
        ImGui::GetIO().AddMousePosEvent(x, 20.0f);
        draw();
        for (int i = 0; i < 6; ++i) {
            if (GImGui->HoveredId == ImHashStr("##label", 0,
                    ImHashStr(names[i], 0, bar->ID)))
                at[i] = x;
        }
    }
    for (int entry = 0; entry < 6; ++entry) {
        REQUIRE(at[entry] > 0.0f);
        ImGui::GetIO().AddMousePosEvent(at[entry], 20.0f);
        draw();
        if (entry == 0) {
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            draw();
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        draw();
        draw();
        CHECK(state.open == entry);
        CHECK(GImGui->OpenPopupStack.Size == 1);
        CHECK(steady_allocations(draw) == 0);
    }

    state.open = hedgr::ui::widget::menu_bar_state::none;
    state.about_open = true;
    draw();
    draw();
    CHECK(steady_allocations(draw) == 0);
}

TEST_CASE("the Instances panel costs a steady frame no allocation, with tiles, "
          "pins, a filter and an empty match",
    "[frame][menu_bar][instances_panel]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    desk desk;
    for (int i = 0; i < 40; ++i) {
        hedgr::model::domain::instance row;
        row.id = std::format("instance-with-a-long-identifier-{}", i);
        row.name = std::format("Instance {}", i);
        static_cast<void>(desk.instances.upsert(row));
    }
    hedgr::model::domain::shortcut pin;
    pin.id = "pin-instances";
    pin.target_kind = hedgr::model::domain::window_kind::instance_manager;
    desk.shortcuts.upsert(pin);
    pin.id = "pin-accounts";
    pin.target_kind = hedgr::model::domain::window_kind::accounts;
    pin.target_id = "instance-with-a-long-identifier-3";
    desk.shortcuts.upsert(pin);
    auto input = desk.input();
    hedgr::ui::widget::menu_bar_state state;
    const std::string svg = "<svg xmlns='http://www.w3.org/2000/svg'/>";
    const auto draw = [&] {
        hedgr::ui::render::menu_bar_result result;
        ImGui::NewFrame();
        hedgr::ui::render::draw_menu_bar(input, svg, true, state, result);
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const ImGuiWindow* bar = ImGui::FindWindowByName("###hedgr-menu-bar");
    REQUIRE(bar != nullptr);
    float label_x = 0.0f;
    for (float x = 2.0f; x < 800.0f && label_x == 0.0f; x += 2.0f) {
        ImGui::GetIO().AddMousePosEvent(x, 20.0f);
        draw();
        if (GImGui->HoveredId == ImHashStr("##label", 0,
                ImHashStr("Instances", 0, bar->ID)))
            label_x = x;
    }
    REQUIRE(label_x > 0.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    for (int i = 0; i < 4; ++i)
        draw();
    REQUIRE(GImGui->OpenPopupStack.Size == 1);
    CHECK(state.instances.matches.size() == 40);
    CHECK(steady_allocations(draw) == 0);

    /* A filter that keeps a few, then one that keeps none. */
    ImGui::GetIO().AddInputCharactersUTF8("INSTANCE 3");
    draw();
    draw();
    CHECK(state.instances.matches.size() == 11);
    CHECK(steady_allocations(draw) == 0);
    ImGui::GetIO().AddInputCharactersUTF8("zz");
    draw();
    draw();
    CHECK(state.instances.matches.empty());
    CHECK(steady_allocations(draw) == 0);
}

TEST_CASE("the ACCOUNTS menu costs a steady frame no allocation, with its "
          "environment submenu open",
    "[frame][menu_bar][accounts]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    desk desk;
    for (int environment = 0; environment < 3; ++environment) {
        const std::string instance_id = std::format("environment-{}",
            environment);
        hedgr::model::domain::instance instance;
        instance.id = instance_id;
        instance.name = std::format("Environment {}", environment);
        static_cast<void>(desk.instances.upsert(instance));

        hedgr::model::domain::folder root;
        root.id = hedgr::model::domain::accounts_root_id_for(instance_id);
        root.owner_id = instance_id;
        root.name = "Accounts";
        root.tree = hedgr::model::domain::folder_tree::accounts;
        REQUIRE(desk.folders.upsert(root).has_value());

        hedgr::model::domain::folder privileged;
        privileged.id = std::format("privileged-{}", environment);
        privileged.parent_id = root.id;
        privileged.owner_id = instance_id;
        privileged.name = "Privileged";
        privileged.tree = hedgr::model::domain::folder_tree::accounts;
        REQUIRE(desk.folders.upsert(privileged).has_value());

        for (int i = 0; i < 10; ++i) {
            hedgr::model::domain::account row;
            row.id = std::format("account-{}-{}", environment, i);
            row.instance_id = instance_id;
            row.name = std::format("Account {}", i);
            row.principal = std::format("user{}@example", i);
            row.folder_id = privileged.id;
            row.has_credential = true;
            static_cast<void>(desk.accounts.upsert(row));
        }
    }
    auto input = desk.input();
    hedgr::ui::widget::menu_bar_state state;
    const std::string svg = "<svg xmlns='http://www.w3.org/2000/svg'/>";
    const auto draw = [&] {
        hedgr::ui::render::menu_bar_result result;
        ImGui::NewFrame();
        hedgr::ui::render::draw_menu_bar(input, svg, true, state, result);
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const ImGuiWindow* bar = ImGui::FindWindowByName("###hedgr-menu-bar");
    REQUIRE(bar != nullptr);
    float label_x = 0.0f;
    for (float x = 2.0f; x < 800.0f && label_x == 0.0f; x += 2.0f) {
        ImGui::GetIO().AddMousePosEvent(x, 20.0f);
        draw();
        if (GImGui->HoveredId == ImHashStr("##label", 0,
                ImHashStr("Accounts", 0, bar->ID)))
            label_x = x;
    }
    REQUIRE(label_x > 0.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    for (int i = 0; i < 4; ++i)
        draw();
    REQUIRE(GImGui->OpenPopupStack.Size == 1);
    CHECK(state.accounts.environments.size() == 3);

    /* Open the first environment's submenu. */
    const ImGuiWindow* menu = GImGui->OpenPopupStack.back().Window;
    const float row = ImGui::GetTextLineHeight()
        + 2.0f * hedgr::ui::theme::menu_row_padding_y;
    ImGui::GetIO().AddMousePosEvent(menu->Pos.x + 30.0f,
        menu->Pos.y + hedgr::ui::theme::menu_padding + row * 0.5f);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    for (int i = 0; i < 4; ++i)
        draw();
    REQUIRE(GImGui->OpenPopupStack.Size == 2);
    CHECK(steady_allocations(draw) == 0);
}

TEST_CASE("the status strip costs a steady frame no allocation, silent and with "
          "a message",
    "[frame][status_bar]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    desk desk;
    desk.sessions.set_account_result(desk.session, "Acme", "Party", "pid",
        hedgr::model::domain::secret_string{"token"});
    auto input = desk.input();
    input.now_ns = 1000;
    hedgr::ui::widget::status_bar status;
    const auto draw = [&] {
        ImGui::NewFrame();
        hedgr::ui::render::draw_status_bar(input, status);
        ImGui::Render();
    };
    draw();
    draw();
    CHECK(steady_allocations(draw) == 0);

    status.show_event("the store was opened from the environment", 1000);
    draw();
    draw();
    CHECK(steady_allocations(draw) == 0);
}

TEST_CASE("the floating toolbar costs a steady frame no allocation",
    "[frame][toolbar]") {
    headless_imgui imgui;
    hedgr::ui::theme::apply_theme();
    desk desk;
    auto input = desk.input();
    const auto draw = [&] {
        hedgr::ui::render::toolbar_result result;
        ImGui::NewFrame();
        hedgr::ui::render::draw_toolbar(input, true, result);
        ImGui::Render();
    };
    draw();
    draw();
    REQUIRE(ImGui::FindWindowByName("###hedgr-toolbar") != nullptr);
    CHECK(steady_allocations(draw) == 0);

    /* With the pointer over a button, for its hover fill and tooltip. */
    ImGui::GetIO().AddMousePosEvent(
        ImGui::FindWindowByName("###hedgr-toolbar")->Pos.x + 80.0f,
        ImGui::FindWindowByName("###hedgr-toolbar")->Pos.y + 20.0f);
    draw();
    draw();
    CHECK(steady_allocations(draw) == 0);
}
