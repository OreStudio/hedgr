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
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

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

TEST_CASE("a mini plot card for a series not yet held draws and says it is reading",
    "[mini_plot]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::desk desk;
    const auto handle = desk.open(model::domain::window_kind::mini_plot,
        model::domain::mini_plot_state{.label = "FX GBP/USD", .series_id = "s"});

    const auto text = drawn_text(desk, handle);
    CHECK(text.find("FX GBP/USD") != std::string::npos);
    CHECK(text.find("Reading the series") != std::string::npos);
}
