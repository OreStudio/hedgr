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
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/term_ladder.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
using hedgr::testing::desk;
using hedgr::testing::frame;
using hedgr::testing::headless_imgui;

model::domain::term_curve curve_of(std::vector<std::string> tenors,
    double base, std::int64_t at_ns) {
    model::domain::term_curve curve{.at_ns = at_ns};
    double value = base;
    for (auto& tenor : tenors)
        curve.nodes.push_back({.tenor = std::move(tenor), .value = value += 0.001,
            .at_ns = at_ns});
    return curve;
}

/** @brief The unordered tenors, where string order and rank order differ. */
const std::vector<std::string> unordered{"10Y", "2Y", "1M", "6M", "ON", "3M",
    "1Y", "5Y", "1W", "30Y", "2W", "9M"};

/** @brief The ladder as the window drew it before it was cached. */
std::vector<model::domain::term_node> sorted_nodes(
    std::vector<model::domain::term_node> nodes) {
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        return render::tenor_rank(a.tenor) < render::tenor_rank(b.tenor);
    });
    return nodes;
}

}

TEST_CASE("the ladder is the primary curve's tenors in rank order",
    "[rates][ladder]") {
    const std::vector<model::domain::term_curve> curves{
        curve_of({"1M", "1Y"}, 0.02, 1), curve_of(unordered, 0.03, 2)};
    render::term_ladder ladder;
    REQUIRE(render::update_term_ladder(ladder, {.version = 1, .history = 5},
        curves, 0, 1));

    const auto expected = sorted_nodes(curves[1].nodes);
    REQUIRE(ladder.rungs.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(ladder.rungs[i].tenor == expected[i].tenor);
        CHECK(ladder.xs[i] == static_cast<double>(i));
    }
    CHECK(std::string{ladder.labels[0]} == "ON");
    CHECK(std::string{ladder.labels[1]} == "1W");
    CHECK(std::string{ladder.labels.back()} == "30Y");
}

TEST_CASE("an empty tenor is labelled spot and sorts first", "[rates][ladder]") {
    const std::vector<model::domain::term_curve> curves{
        curve_of({"1Y", "", "1M"}, 0.02, 1)};
    render::term_ladder ladder;
    render::update_term_ladder(ladder, {.version = 1, .history = 1}, curves, 0,
        0);
    CHECK(std::string{ladder.labels[0]} == "spot");
    CHECK(std::string{ladder.labels[2]} == "1Y");
}

TEST_CASE("each overlay lines up with the ladder, absent tenors are NaN",
    "[rates][ladder]") {
    const std::vector<model::domain::term_curve> curves{
        curve_of({"1Y", "1M"}, 0.02, 1), curve_of(unordered, 0.03, 2)};
    render::term_ladder ladder;
    render::update_term_ladder(ladder, {.version = 1, .history = 5}, curves,
        0, 1);

    REQUIRE(ladder.ys.size() == 2);
    for (std::size_t c = 0; c < 2; ++c) {
        std::map<std::string, double> by_tenor;
        for (const auto& node : curves[c].nodes)
            by_tenor[node.tenor] = node.value;
        for (std::size_t i = 0; i < ladder.rungs.size(); ++i) {
            const auto found = by_tenor.find(ladder.rungs[i].tenor);
            if (found == by_tenor.end())
                CHECK(std::isnan(ladder.ys[c][i]));
            else
                CHECK(ladder.ys[c][i] == found->second);
        }
    }
}

TEST_CASE("a repeated tenor draws its last value, as the map did",
    "[rates][ladder]") {
    model::domain::term_curve curve{.at_ns = 1,
        .nodes = {{.tenor = "1Y", .value = 1.0}, {.tenor = "1Y", .value = 2.0},
            {.tenor = "1M", .value = 3.0}}};
    render::term_ladder ladder;
    render::update_term_ladder(ladder, {.version = 1, .history = 1}, {curve},
        0, 0);
    const auto at = ladder.position_of("1Y");
    REQUIRE(at < ladder.rungs.size());
    CHECK(ladder.ys[0][at] == 2.0);
    CHECK(ladder.position_of("7Y") == ladder.rungs.size());
}

TEST_CASE("the ladder rebuilds on a new version or history and not otherwise",
    "[rates][ladder]") {
    const std::vector<model::domain::term_curve> curves{
        curve_of(unordered, 0.03, 1), curve_of(unordered, 0.04, 2)};
    render::term_ladder ladder;
    CHECK(render::update_term_ladder(ladder, {.version = 3, .history = 2},
        curves, 0, 1));
    CHECK_FALSE(render::update_term_ladder(ladder, {.version = 3, .history = 2},
        curves, 0, 1));
    CHECK(ladder.builds == 1);
    CHECK(render::update_term_ladder(ladder, {.version = 4, .history = 2},
        curves, 0, 1));
    CHECK(render::update_term_ladder(ladder, {.version = 4, .history = 1},
        curves, 1, 1));
    CHECK(ladder.ys.size() == 1);
    CHECK(ladder.builds == 3);
}

TEST_CASE("a copied ladder starts empty so its labels never dangle",
    "[rates][ladder]") {
    const std::vector<model::domain::term_curve> curves{
        curve_of(unordered, 0.03, 1)};
    render::term_ladder ladder;
    render::update_term_ladder(ladder, {.version = 1, .history = 1}, curves, 0,
        0);
    const render::term_ladder copy{ladder};
    CHECK(copy.labels.empty());
    CHECK(render::term_ladder{copy}.key == render::term_ladder_key{});
}

TEST_CASE("the window rebuilds its ladder on a new read and a history change",
    "[rates][ladder][frame]") {
    headless_imgui imgui;
    desk desk;
    desk.term_structures.set("i", "a", "k", "IR GBP",
        {curve_of(unordered, 0.03, 1'000'000'000),
            curve_of(unordered, 0.04, 2'000'000'000)});
    const auto handle = desk.open(model::domain::window_kind::term_structure,
        model::domain::term_structure_state{.label = "IR GBP", .curve_key = "k"});
    render::window_ui_state state;
    state.market_view.term_history = 2;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();
    const auto draw = [&] {
        frame([&] {
            render::render_term_structure(handle, input, state, status, lock);
        });
    };

    draw();
    draw();
    CHECK(state.ladder.builds == 1);
    CHECK(state.ladder.ys.size() == 2);

    desk.term_structures.begin("i", "a", "k", "IR GBP");
    draw();
    CHECK(state.ladder.builds == 2);

    desk.term_structures.set("i", "a", "k", "IR GBP",
        {curve_of(unordered, 0.05, 3'000'000'000)});
    draw();
    CHECK(state.ladder.builds == 3);
    CHECK(state.ladder.ys.size() == 1);

    state.market_view.term_history = 1;
    draw();
    draw();
    CHECK(state.ladder.builds == 4);
}
