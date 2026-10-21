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
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/tabs.hpp"
#include "imgui.h"
#include "software_raster.hpp"
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;

constexpr float gallery_width = 500.0f;
constexpr float gallery_height = 560.0f;

void signal_card() {
    if (widget::begin_section_card("signal", gallery_width - 40.0f)) {
        widget::value_pill("BTC");
        widget::section_card_divider();
        widget::hero_strip("Current P/L", "+0.59%", theme::tone::good,
            "TARGETING TP2", "Profit since entry");
        widget::section_card_divider();
        const float cell = ImGui::GetContentRegionAvail().x / 3.0f;
        widget::labelled_value("Date", "15 Oct 2026", cell, "Opened");
        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("TF", "8-HOUR", theme::tone::neutral, cell, "Bar");
        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("Type", "SHORT", theme::tone::bad, cell, "Side");
        widget::labelled_pill("Category", "ALPHA", theme::tone::neutral, cell,
            "Set");
        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("Power", "STRONG", theme::tone::neutral, cell,
            "Strength");
        ImGui::SameLine(0.0f, 0.0f);
        widget::labelled_pill("Side", "LONG", theme::tone::good, cell);
    }
    widget::end_section_card();
}

void variants_card() {
    if (widget::begin_section_card("variants", gallery_width - 40.0f)) {
        widget::caption("Captions");
        widget::section_card_divider();
        widget::caption("Plain caption");
        widget::caption("Caption with help", "The help text");
        widget::section_card_divider();
        widget::caption("Pills");
        widget::value_pill("NEUTRAL", theme::tone::neutral);
        ImGui::SameLine();
        widget::value_pill("GOOD", theme::tone::good);
        ImGui::SameLine();
        widget::value_pill("BAD", theme::tone::bad);
        ImGui::SameLine();
        widget::value_pill("WARNING", theme::tone::warning);
        widget::section_card_divider();
        widget::caption("Hero strips");
        widget::hero_strip("Current P/L", "-1.20%", theme::tone::bad,
            "STOPPED", "Profit since entry");
        widget::hero_strip("Drawdown", "4.10%", theme::tone::warning,
            "NEAR LIMIT");
        widget::hero_strip("Notional", "1,250,000", theme::tone::neutral);
    }
    widget::end_section_card();
}

void gallery() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(gallery_width, gallery_height));
    ImGui::Begin("gallery", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTabBar("tabs")) {
        for (const char* label : {"All Signals", "My Pairs", "Tracked"}) {
            const bool tracked = label[0] == 'T';
            if (ImGui::BeginTabItem(label, nullptr,
                    tracked ? ImGuiTabItemFlags_SetSelected : 0)) {
                widget::underline_selected_tab();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    signal_card();
    variants_card();
    ImGui::End();
}

}

/*
 * Hidden from the suite: it writes a picture for a person to look at and
 * asserts nothing about it. Run it by name with HEDGR_GALLERY_PPM set.
 */
TEST_CASE("component gallery", "[.gallery]") {
    const char* out = std::getenv("HEDGR_GALLERY_PPM");
    REQUIRE(out != nullptr);

    hedgr::testing::headless_imgui imgui;
    ImGui::GetIO().DisplaySize = ImVec2(gallery_width, gallery_height);
    hedgr::ui::icon::load_ui_fonts(15.0f);
    theme::apply_theme();

    hedgr::testing::software_raster raster(static_cast<int>(gallery_width),
        static_cast<int>(gallery_height));
    for (int i = 0; i < 3; ++i) {
        ImGui::NewFrame();
        gallery();
        ImGui::Render();
        raster.update_textures();
    }
    raster.draw(*ImGui::GetDrawData(), IM_COL32(0, 0, 0, 255));
    CHECK(raster.write_ppm(std::filesystem::path(out)));
}
