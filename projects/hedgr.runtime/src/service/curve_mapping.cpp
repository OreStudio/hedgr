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
#include "hedgr.runtime/service/curve_mapping.hpp"
#include "hedgr.model/event/event.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace hedgr::runtime::service {

namespace {

[[nodiscard]] constexpr double curve_gap() {
    return std::numeric_limits<double>::quiet_NaN();
}

}

curve_reading read_published_curves(
    std::span<const hedgr::session::transport::published_curve> sets) {
    using hedgr::session::transport::published_curve;

    std::vector<const published_curve*> instants;
    instants.reserve(sets.size());
    for (const auto& set : sets)
        if (!set.points.empty())
            instants.push_back(&set);
    std::stable_sort(instants.begin(), instants.end(),
        [](const published_curve* left, const published_curve* right) {
            return left->as_of_ns < right->as_of_ns;
        });

    curve_reading reading;
    if (instants.empty())
        return reading;
    reading.label = instants.front()->label;

    std::unordered_map<std::string, std::size_t> pillar_at;
    for (const auto* set : instants) {
        for (const auto& point : set->points) {
            if (point.pillar.empty() || !point.numeric)
                continue;
            if (pillar_at.contains(point.pillar))
                continue;
            pillar_at.emplace(point.pillar, reading.pillars.size());
            reading.pillars.push_back(point.pillar);
        }
    }
    if (reading.pillars.empty())
        return reading;

    const auto count = reading.pillars.size();
    std::vector<double> carried(count, curve_gap());
    reading.as_of_ns.reserve(instants.size());
    reading.values.reserve(instants.size() * count);
    for (const auto* set : instants) {
        for (const auto& point : set->points) {
            if (!point.numeric)
                continue;
            if (const auto found = pillar_at.find(point.pillar);
                found != pillar_at.end())
                carried[found->second] = point.value;
        }
        reading.as_of_ns.push_back(set->as_of_ns);
        reading.values.insert(
            reading.values.end(), carried.begin(), carried.end());
    }
    return reading;
}

bool apply_curve_event(model::state::curve_table& curves,
    model::state::curve_handle handle,
    const hedgr::model::event::event& event) {
    if (!event.message.empty()) {
        curves.fail(handle, event.message);
        return false;
    }
    if (event.curve_pillars.empty() || event.curve_as_of_ns.empty())
        return false;
    curves.apply(handle, event.curve_label, event.curve_pillars,
        event.curve_as_of_ns, event.curve_values);
    return true;
}

}
