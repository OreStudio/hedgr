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
#include "hedgr.ui/widget/capture.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <string>
#include <vector>

using hedgr::ui::widget::capture_step;
using hedgr::ui::widget::capture_step_kind;
using hedgr::ui::widget::parse_capture_steps;

namespace {

/** @brief The steps a script parses to, for a test that reads like the script. */
[[nodiscard]] std::vector<capture_step> steps_of(std::string_view script) {
    return parse_capture_steps(script);
}

}

TEST_CASE("a script reads one gesture per line", "[capture]") {
    const auto steps = steps_of(R"(
click Development
click_arrow Development
expand Production
collapse Instances
focus "Brave Hopper"
)");

    REQUIRE(steps.size() == 5);
    REQUIRE(steps[0].kind == capture_step_kind::click);
    REQUIRE(steps[0].target == "Development");
    REQUIRE(steps[1].kind == capture_step_kind::click_arrow);
    REQUIRE(steps[1].target == "Development");
    REQUIRE(steps[2].kind == capture_step_kind::expand);
    REQUIRE(steps[2].target == "Production");
    REQUIRE(steps[3].kind == capture_step_kind::collapse);
    REQUIRE(steps[3].target == "Instances");
    REQUIRE(steps[4].kind == capture_step_kind::focus);
    REQUIRE(steps[4].target == "Brave Hopper");
}

TEST_CASE("a step names the line it was written on", "[capture]") {
    const auto steps = steps_of("click Development\n# a remark\n\nclick Production\n");

    REQUIRE(steps.size() == 2);
    REQUIRE(steps[0].line == 1);
    REQUIRE(steps[1].line == 4);
}

TEST_CASE("a row name may hold spaces and a label follows a bar", "[capture]") {
    const auto steps = steps_of(
        "focus \"Brave Hopper\"\nclick instance-manager | Probe every instance\n");

    REQUIRE(steps.size() == 2);
    REQUIRE(steps[0].target == "Brave Hopper");
    REQUIRE(steps[0].label.empty());
    REQUIRE(steps[1].target == "instance-manager");
    REQUIRE(steps[1].label == "Probe every instance");
}

TEST_CASE("a name of two words without quotes is one name and one label", "[capture]") {
    const auto steps = steps_of("click Brave Hopper\n");

    /*
     * Deliberately not two words of one name. A step whose fields were decided
     * by adjacency would mean whichever of the two the reader assumed, and the
     * quoting above is what makes the intent sayable.
     */
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].target == "Brave");
    REQUIRE(steps[0].label.empty());
}

TEST_CASE("a line that names no gesture is dropped rather than guessed", "[capture]") {
    const auto steps = steps_of("open Development\nclick\n\nclick Production\n");

    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].target == "Production");
}

TEST_CASE("an empty script drives nothing", "[capture]") {
    REQUIRE(steps_of("").empty());
    REQUIRE(steps_of("\n\n\n").empty());
}
