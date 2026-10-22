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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/workspace.hpp"
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
            .asset_class = "FX", .producer_kind = "SYNTHETIC"});
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

TEST_CASE("a currency outside the built-in artwork is drawn from its server image",
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

TEST_CASE("a currency with no server image and no artwork draws nothing",
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

TEST_CASE("a currency with artwork and no server image falls back to the artwork",
    "[frame][flags]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {{"USD", "img-us"}});

    const auto drawn = draw_flags_for(desk, "IR BRL 1Y");

    CHECK(drawn.texture_gets == 0);
    CHECK(drawn.vertices > 0);
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
            .producer_kind = "SYNTHETIC",
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
       two left are the chrome's identity and footer text, not the rows. */
    CHECK(small <= 2);
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
            .producer_kind = "SYNTHETIC", .curve_key = "s-0",
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

TEST_CASE("an IR curve row falls back to the built-in flag",
    "[frame][flags][browser]") {
    headless_imgui imgui;
    desk desk;
    desk.currency_images.set("t", {});

    const auto drawn = draw_listed_row(desk, "IR", "IR HUF IR_SWAP");
    const auto bare = draw_listed_row(desk, "IR", "IR ZZZ IR_SWAP");

    CHECK(drawn.asked.empty());
    CHECK(drawn.flag_draws == 0);
    CHECK(desk.textures.gets == 0);
    CHECK(drawn.text_x == bare.text_x + 14.0f);
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
