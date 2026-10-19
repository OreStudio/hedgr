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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/image/svg_raster.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using hedgr::ui::widget::currencies_of;
using hedgr::ui::widget::has_flag;

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

TEST_CASE("a currency the build does not know has no flag", "[flags]") {
    REQUIRE_FALSE(has_flag("XXX"));
    REQUIRE(currencies_of("FX XXX/USD").base.empty());
}

TEST_CASE("the flag list covers the currencies the deployment quotes", "[flags]") {
    for (const auto* code : {"GBP", "USD", "EUR", "JPY", "CHF", "AUD", "NZD",
             "CAD", "DKK", "SEK", "NOK"})
        REQUIRE(has_flag(code));
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
