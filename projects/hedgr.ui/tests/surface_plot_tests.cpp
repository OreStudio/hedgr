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

#include "hedgr.model/state/surface_table.hpp"
#include "hedgr.ui/render/surface_plot.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace state = hedgr::model::state;
namespace render = hedgr::ui::render;

/** @brief A surface table holding one published grid; its handle and row. */
struct published final {
    state::surface_table surfaces;
    state::surface_handle handle;
    const state::surface_row* row = nullptr;

    published(std::vector<std::string> expiries, std::vector<std::string> strikes,
        std::vector<double> values) {
        handle = surfaces.intern("i", "a", "bp-1");
        surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD",
            1'760'000'000'000'000'000, std::move(expiries), std::move(strikes),
            std::move(values));
        row = surfaces.get(handle);
    }
};

}

TEST_CASE("the surface plot flattens the grid into the coordinates a plot takes",
    "[surface][plot]") {
    /*
     * Two expiries by three strikes, row-major: the expiry varies slowest. The
     * x coordinate is the strike's position and the y the expiry's, because both
     * are names rather than numbers.
     */
    const published grid({"1M", "2M"}, {"ATM", "25D", "10D"},
        {0.08, 0.09, 0.10, 0.11, 0.12, 0.13});
    REQUIRE(grid.row != nullptr);
    render::surface_plot plot;
    CHECK(render::update_surface_plot(plot,
        {.version = grid.surfaces.version(grid.handle),
            .as_of_ns = grid.row->as_of_ns},
        *grid.row, grid.surfaces));

    REQUIRE(plot.x_count == 3);
    REQUIRE(plot.y_count == 2);
    REQUIRE(plot.xs.size() == 6);
    CHECK(plot.xs[0] == 0.0);
    CHECK(plot.xs[1] == 1.0);
    CHECK(plot.xs[2] == 2.0);
    CHECK(plot.xs[3] == 0.0);
    CHECK(plot.ys[0] == 0.0);
    CHECK(plot.ys[3] == 1.0);
    CHECK(plot.zs[0] == 0.08);
    CHECK(plot.zs[2] == 0.10);
    CHECK(plot.zs[3] == 0.11);
    CHECK(plot.zs[5] == 0.13);
    CHECK(plot.low == 0.08);
    CHECK(plot.high == 0.13);

    /* The axis ticks are the names the blueprint stated, in the grid's order. */
    CHECK(plot.strike_text
        == std::vector<std::string>{"ATM", "25D", "10D"});
    CHECK(plot.expiry_text == std::vector<std::string>{"1M", "2M"});
}

TEST_CASE("the surface plot is rebuilt only when the surface moved",
    "[surface][plot]") {
    const published grid({"1M"}, {"ATM"}, {0.08});
    REQUIRE(grid.row != nullptr);
    render::surface_plot plot;
    const render::surface_plot_key key{
        .version = grid.surfaces.version(grid.handle),
        .as_of_ns = grid.row->as_of_ns};
    CHECK(render::update_surface_plot(plot, key, *grid.row, grid.surfaces));
    CHECK(plot.builds == 1);
    CHECK_FALSE(
        render::update_surface_plot(plot, key, *grid.row, grid.surfaces));
    CHECK(plot.builds == 1);

    /* A republish at the same version is still a new surface. */
    CHECK(render::update_surface_plot(plot,
        {.version = key.version, .as_of_ns = key.as_of_ns + 1}, *grid.row,
        grid.surfaces));
    CHECK(plot.builds == 2);
}

TEST_CASE("an incomplete grid is not plotted", "[surface][plot]") {
    /*
     * A grid with fewer values than its shape says is a read that has not
     * finished. Drawing the hole as a zero volatility would put a cliff in a
     * surface the deployment never stated.
     */
    const published grid({"1M", "2M"}, {"ATM", "25D"}, {0.08, 0.09});
    REQUIRE(grid.row != nullptr);
    render::surface_plot plot;
    CHECK(render::update_surface_plot(plot, {.version = 1, .as_of_ns = 0},
        *grid.row, grid.surfaces));
    CHECK(plot.x_count == 0);
    CHECK(plot.y_count == 0);
    CHECK(plot.zs.empty());
}

TEST_CASE("the surface plot's range ignores values that are not finite",
    "[surface][plot]") {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const published grid({"1M", "2M"}, {"ATM", "25D"}, {nan, 0.09, 0.11, nan});
    REQUIRE(grid.row != nullptr);
    render::surface_plot plot;
    CHECK(render::update_surface_plot(plot, {.version = 1, .as_of_ns = 0},
        *grid.row, grid.surfaces));
    CHECK(plot.low == 0.09);
    CHECK(plot.high == 0.11);
}
