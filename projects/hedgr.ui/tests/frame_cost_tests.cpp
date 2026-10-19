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
    desk.market.set_catalogue("i", "a", std::move(catalogue));
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
