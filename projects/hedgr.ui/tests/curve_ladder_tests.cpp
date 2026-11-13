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
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "headless_desk.hpp"
#include "hedgr.ui/render/curve_ladder.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

using hedgr::testing::desk;

/** @brief The newest curve instant the ladder reads, one minute old. */
constexpr std::int64_t curve_at_ns = 1'760'000'000'000'000'000;

/**
 * @brief A desk holding one curve of three rungs, and its window's state.
 *
 * The curve is seeded with a single instant, so what the ladder shows is
 * exactly what one whole-curve read said. The members are the three series a
 * tick ladder is built from, present in the catalogue and held by the window.
 */
struct ladder_desk final {
    hedgr::testing::headless_imgui imgui;
    desk desk;
    model::domain::term_structure_state terms;
    model::state::curve_handle curve;

    ladder_desk() {
        desk.market.apply_catalogue("i", "a",
            {member_row("s-on", "ON"), member_row("s-1m", "1M"),
                member_row("s-1y", "1Y")});
        curve = hedgr::testing::seed_curve(desk.curves, "i", "a", "curve-key",
            "IR AUD DISCOUNT AUD-AONIA", 1, curve_at_ns);
        terms.label = "IR AUD DISCOUNT AUD-AONIA";
        terms.curve_key = "curve-key";
        terms.member_ids = {"s-on", "s-1m", "s-1y"};
        terms.member_tenors = {"ON", "1M", "1Y"};
    }

    static model::domain::market_series_row member_row(std::string id,
        std::string tenor) {
        model::domain::market_series_row row;
        row.id = std::move(id);
        row.label = "IR AUD " + tenor;
        row.asset_class = "IR";
        row.tenor = std::move(tenor);
        return row;
    }

    /** @brief Appends one tick to a member series, at @p at_ns. */
    void tick(std::string_view id, std::int64_t at_ns, double value) {
        const auto handle = desk.market.intern("i", "a", id);
        static_cast<void>(desk.market.append(handle,
            model::domain::market_level::fine,
            model::state::series_point{.at_ns = at_ns, .value = value}, 1));
    }

    [[nodiscard]] std::vector<render::ladder_rung> ladder() {
        auto input = desk.input();
        const auto& row = *desk.curves.get(curve);
        const auto& axis = desk.curves.axis(row.axis);
        std::vector<render::ladder_rung> rungs(axis.pillars.size());
        render::curve_ladder(input, desk.session, terms, row, axis, rungs);
        return rungs;
    }
};

}

TEST_CASE("a ladder with no ticks reads the curve it is drawn under",
    "[curve][ladder]") {
    ladder_desk d;

    const auto rungs = d.ladder();

    /* The seeded curve holds ten pillars; the ladder has one row per pillar. */
    REQUIRE(rungs.size() == 10);
    CHECK(std::string{rungs.front().label} == "ON");
    REQUIRE(rungs.front().mid.has_value());
    CHECK(*rungs.front().mid == Catch::Approx(0.038));
    CHECK(rungs.front().at_ns == curve_at_ns);
    /*
     * Nothing is ticking, because no member series has a point: the value is
     * the whole-curve read's, which is as fresh as the last read.
     */
    CHECK_FALSE(rungs.front().ticking);
}

TEST_CASE("a rung's own tick is what the ladder shows once it is newer",
    "[curve][ladder]") {
    ladder_desk d;
    d.tick("s-on", curve_at_ns + 30'000'000'000, 0.0415);
    d.tick("s-1m", curve_at_ns + 30'000'000'000, 0.0425);

    const auto rungs = d.ladder();

    /* ON and 1M tick; 1Y still has no series point. */
    CHECK(rungs[0].ticking);
    CHECK(*rungs[0].mid == Catch::Approx(0.0415));
    CHECK(rungs[0].at_ns == curve_at_ns + 30'000'000'000);
    CHECK(rungs[2].ticking);
    CHECK(*rungs[2].mid == Catch::Approx(0.0425));
    /*
     * A rung with no tick of its own keeps the curve's value, which is the
     * value the plot above is drawing at that pillar.
     */
    CHECK_FALSE(rungs[4].ticking);
    CHECK(*rungs[4].mid
        == Catch::Approx(0.038 + 0.003 * std::log1p(4.0)));
}

TEST_CASE("an older tick does not displace the curve's value",
    "[curve][ladder]") {
    ladder_desk d;
    /* A tick from before the curve was read is history, not a quote. */
    d.tick("s-on", curve_at_ns - 60'000'000'000, 0.0200);

    const auto rungs = d.ladder();

    CHECK_FALSE(rungs.front().ticking);
    CHECK(*rungs.front().mid == Catch::Approx(0.038));
    CHECK(rungs.front().at_ns == curve_at_ns);
}

TEST_CASE("a rung whose pillar has no member still shows the curve's value",
    "[curve][ladder]") {
    ladder_desk d;
    /* The window holds three members; the curve has ten pillars. */
    const auto& row = *d.desk.curves.get(d.curve);
    const auto& axis = d.desk.curves.axis(row.axis);
    CHECK(axis.pillars.size() == 10);
    CHECK(d.terms.member_ids.size() == 3);

    auto input = d.desk.input();
    std::array<render::ladder_rung, 10> rungs;
    render::curve_ladder(input, d.desk.session, d.terms, row, axis, rungs);

    for (std::size_t i = 0; i < rungs.size(); ++i) {
        REQUIRE(rungs[i].mid.has_value());
        CHECK_FALSE(rungs[i].ticking);
    }
    CHECK(std::string{rungs[9].label} == "30Y");
}
