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
#include "hedgr.runtime/service/probe_backoff.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {

using namespace std::chrono_literals;
using hedgr::model::domain::reachability;
using hedgr::model::domain::readiness;
using hedgr::runtime::service::next_probe_interval;
using hedgr::runtime::service::probe_answered;
using hedgr::runtime::service::probe_cadence;
using hedgr::runtime::service::probe_cadence_max;

}

TEST_CASE("a probe that got no answer doubles its wait up to the cap",
    "[probe_backoff]") {
    auto wait = 0ms;
    wait = next_probe_interval(wait, false);
    CHECK(wait == probe_cadence * 2);
    wait = next_probe_interval(wait, false);
    CHECK(wait == probe_cadence * 4);
    wait = next_probe_interval(wait, false);
    CHECK(wait == probe_cadence * 8);
    for (int i = 0; i < 10; ++i)
        wait = next_probe_interval(wait, false);
    CHECK(wait == probe_cadence_max);
}

TEST_CASE("an answer returns the probe to its cadence", "[probe_backoff]") {
    CHECK(next_probe_interval(probe_cadence_max, true) == probe_cadence);
    CHECK(next_probe_interval(0ms, true) == probe_cadence);
}

TEST_CASE("a broker that answered while IAM did not is not an answer",
    "[probe_backoff]") {
    CHECK_FALSE(probe_answered(reachability::unreachable, readiness::unknown));
    CHECK_FALSE(
        probe_answered(reachability::reachable, readiness::service_missing));
    CHECK(probe_answered(reachability::reachable, readiness::ready));
    CHECK(probe_answered(reachability::reachable, readiness::bootstrapping));
}
