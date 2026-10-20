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
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include "implot.h"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
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
