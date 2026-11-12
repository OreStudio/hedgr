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
#include <charconv>
#include <chrono>
#include <cmath>

namespace hedgr::runtime::service {

namespace {

[[nodiscard]] std::optional<double> parse_value(std::string_view text) {
    double value = 0.0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

}

curve_reading read_published_curve(
    const ores::marketdata::messaging::published_curve_set& set) {
    curve_reading reading;
    reading.label = set.curve;
    reading.as_of_ns.push_back(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            set.as_of.time_since_epoch())
            .count());
    for (const auto& point : set.points) {
        const auto value = parse_value(point.value);
        if (!value || point.pillar.empty())
            continue;
        reading.pillars.push_back(point.pillar);
        reading.values.push_back(*value);
    }
    return reading;
}

bool apply_published_curve(model::state::curve_table& curves,
    model::state::curve_handle handle,
    const ores::marketdata::messaging::published_curve_set& set) {
    auto reading = read_published_curve(set);
    if (reading.pillars.empty())
        return false;
    curves.apply(handle, std::move(reading.label), std::move(reading.pillars),
        std::move(reading.as_of_ns), std::move(reading.values));
    return true;
}

}
