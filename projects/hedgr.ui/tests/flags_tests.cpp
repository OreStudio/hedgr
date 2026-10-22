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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/image/svg_raster.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>
#include <string>
#include <vector>

using hedgr::ui::widget::currencies_of;

TEST_CASE("a currency pair names both of its currencies", "[flags]") {
    const auto pair = currencies_of("FX GBP/USD");

    REQUIRE(pair.base == "GBP");
    REQUIRE(pair.quote == "USD");
}

TEST_CASE("an interest rate series names its one currency", "[flags]") {
    const auto pair = currencies_of("IR GBP 1D");

    REQUIRE(pair.base == "GBP");
    REQUIRE(pair.quote.empty());
}

TEST_CASE("a label with no currency names none", "[flags]") {
    const auto pair = currencies_of("EQ AAPL");

    REQUIRE(pair.base.empty());
    REQUIRE(pair.quote.empty());
}

TEST_CASE("any ISO shaped currency is named", "[flags]") {
    CHECK(currencies_of("IR TWD IR_SWAP").base == "TWD");
    CHECK(currencies_of("FX TWD/USD").base == "TWD");
}

TEST_CASE("a word that is not a currency code is not named", "[flags]") {
    CHECK(currencies_of("EQ AAPL").base.empty());
    CHECK(currencies_of("IR twd 1M").base.empty());
    CHECK(currencies_of("FX GBPX/USD").base.empty());
}

TEST_CASE("a principal and a tenant read as one line", "[identity]") {
    hedgr::ui::render::identity_bar_state state;
    state.signed_in = true;
    state.principal = "super_admin";
    state.tenant = "Root Tenant";
    state.party = "System Party";

    REQUIRE(hedgr::ui::render::identity_text(state)
        == "super_admin \u00b7 Root Tenant \u00b7 System Party");
}

TEST_CASE("the identity keeps the tenant and party placeholders", "[identity]") {
    hedgr::ui::render::identity_bar_state state;
    state.signed_in = true;
    state.principal = "tenant_admin@acme_corporation";

    REQUIRE(hedgr::ui::render::identity_text(state)
        == "tenant_admin@acme_corporation \u00b7 no tenant \u00b7 no party");
}

TEST_CASE("a window with no session says so rather than going blank", "[identity]") {
    const hedgr::ui::render::identity_bar_state state;

    REQUIRE(hedgr::ui::render::identity_text(state) == "Not signed in");
}

TEST_CASE("an svg flag rasterises to opaque rgba", "[flags]") {
    const auto image = hedgr::ui::image::rasterize_svg(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 11">)"
        R"(<rect width="16" height="11" fill="#c8102e"/></svg>)",
        32, 22);

    REQUIRE(image.has_value());
    REQUIRE(image->width == 32);
    REQUIRE(image->height == 22);
    REQUIRE(image->pixels.size() == 32u * 22u * 4u);
    CHECK(image->pixels[0] == 0xc8);
    CHECK(image->pixels[1] == 0x10);
    CHECK(image->pixels[2] == 0x2e);
    CHECK(image->pixels[3] == 0xff);
}

TEST_CASE("an unusable svg yields nothing", "[flags]") {
    CHECK_FALSE(hedgr::ui::image::rasterize_svg("<svg", 16, 11).has_value());
    CHECK_FALSE(hedgr::ui::image::rasterize_svg("", 16, 11).has_value());
}

namespace {

namespace widget = hedgr::ui::widget;
namespace theme = hedgr::ui::theme;

constexpr ImTextureID served = 7;
constexpr ImTextureID other = 9;

/** @brief The vertices a draw list sent with @p texture. */
struct textured final {
    std::vector<ImDrawVert> vertices;
    std::size_t commands = 0;
};

textured vertices_with(const ImDrawList* list, ImTextureID texture) {
    textured result;
    for (const auto& cmd : list->CmdBuffer) {
        if (cmd.GetTexID() != texture)
            continue;
        ++result.commands;
        for (unsigned int i = 0; i < cmd.ElemCount; ++i)
            result.vertices.push_back(list->VtxBuffer[static_cast<int>(
                list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]
                + cmd.VtxOffset)]);
    }
    return result;
}

}

TEST_CASE("flags that are not loaded reserve no width", "[flags]") {
    constexpr float height = 22.0f;
    const float one = widget::flag_width_for(height);

    CHECK(widget::flag_pair_width(height, 0, 0) == 0.0f);
    CHECK(widget::flag_pair_width(height, served, 0) == one);
    CHECK(widget::flag_pair_width(height, 0, served) == one);
    CHECK(widget::flag_pair_width(height, served, other)
        == 2.0f * one + theme::flag_pair_gap);
}

TEST_CASE("no texture draws no flag", "[flags]") {
    hedgr::testing::headless_imgui imgui;
    hedgr::testing::frame([] {
        auto* list = ImGui::GetWindowDrawList();
        const auto before = list->VtxBuffer.Size;

        const float width =
            widget::draw_flag_pair(list, ImVec2(10, 10), 22.0f, 0, 0);

        CHECK(width == 0.0f);
        CHECK(list->VtxBuffer.Size == before);
    });
}

TEST_CASE("a supplied texture draws a rounded, translucent, bordered flag",
    "[flags]") {
    hedgr::testing::headless_imgui imgui;
    ImGui::GetStyle().AntiAliasedFill = false;
    hedgr::testing::frame([] {
        auto* list = ImGui::GetWindowDrawList();
        const auto before = list->VtxBuffer.Size;
        const ImVec2 at(100.0f, 50.0f);

        widget::draw_flag_pair(list, at, 22.0f, served, 0);

        const auto image = vertices_with(list, served);
        REQUIRE(image.commands == 1);
        CHECK(list->VtxBuffer.Size > before + 4);
        for (const auto& v : image.vertices) {
            const bool corner = (v.pos.x == at.x || v.pos.x == at.x + widget::flag_width_for(22.0f))
                && (v.pos.y == at.y || v.pos.y == at.y + 22.0f);
            CHECK_FALSE(corner);
            CHECK(((v.col >> IM_COL32_A_SHIFT) & 0xFF) == theme::flag_alpha);
        }
        const auto border = ImGui::ColorConvertFloat4ToU32(theme::card_border());
        const bool edged = std::any_of(list->VtxBuffer.begin(),
            list->VtxBuffer.end(), [&](const ImDrawVert& v) {
                return (v.col & ~IM_COL32_A_MASK) == (border & ~IM_COL32_A_MASK)
                    && (border & IM_COL32_A_MASK) != 0;
            });
        CHECK(edged);
    });
}

TEST_CASE("the base and quote flags sit the pair gap apart", "[flags]") {
    hedgr::testing::headless_imgui imgui;
    ImGui::GetStyle().AntiAliasedFill = false;
    hedgr::testing::frame([] {
        auto* list = ImGui::GetWindowDrawList();
        constexpr float height = 22.0f;
        const float width = widget::flag_width_for(height);

        widget::draw_flag_pair(list, ImVec2(100.0f, 50.0f), height, served, other);

        const auto base = vertices_with(list, served);
        const auto quote = vertices_with(list, other);
        REQUIRE_FALSE(base.vertices.empty());
        REQUIRE_FALSE(quote.vertices.empty());
        const auto right = [](const textured& t) {
            float edge = t.vertices.front().pos.x;
            for (const auto& v : t.vertices)
                edge = std::max(edge, v.pos.x);
            return edge;
        };
        const auto left = [](const textured& t) {
            float edge = t.vertices.front().pos.x;
            for (const auto& v : t.vertices)
                edge = std::min(edge, v.pos.x);
            return edge;
        };
        CHECK(right(base) == Catch::Approx(100.0f + width));
        CHECK(left(quote) - right(base) == Catch::Approx(theme::flag_pair_gap));
    });
}

TEST_CASE("a heading's flags are three quarters of the price pill, centred on it", "[flags]") {
    hedgr::testing::headless_imgui imgui;
    ImGui::GetStyle().AntiAliasedFill = false;
    hedgr::testing::desk desk;
    auto input = desk.input();
    hedgr::testing::frame([&] {
        auto* list = ImGui::GetWindowDrawList();
        const float pill = widget::value_pill_size("FX GBP/USD").y;

        hedgr::ui::render::draw_series_flags(input, desk.session, "FX GBP/USD");

        const auto image = vertices_with(list, 1);
        REQUIRE_FALSE(image.vertices.empty());
        float top = image.vertices.front().pos.y;
        float bottom = top;
        for (const auto& v : image.vertices) {
            top = std::min(top, v.pos.y);
            bottom = std::max(bottom, v.pos.y);
        }
        CHECK(bottom - top == Catch::Approx(pill * theme::flag_height_fraction));
        const float row_top = ImGui::GetItemRectMin().y;
        CHECK(top - row_top == Catch::Approx(pill * 0.125f));
        CHECK(ImGui::GetItemRectSize().y == Catch::Approx(pill));
    });
}
