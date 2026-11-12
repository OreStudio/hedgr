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
#include "hedgr.runtime/worker/curve_worker.hpp"
#include "hedgr.model/state/curve_table.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <set>
#include <span>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

/** @brief This worker's logger: what a curve read found, and where it failed. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.curve_worker");
    return instance;
}

/**
 * @brief How many instants one curve read asks the deployment for.
 *
 * A read is one call, so the ceiling on how many instants it can name is what
 * a window's history control can dial up to. The model keeps whatever arrives
 * up to its own limit.
 */
constexpr std::uint32_t curve_instants = 32;

[[nodiscard]] constexpr double curve_gap() {
    return std::numeric_limits<double>::quiet_NaN();
}

}

service::curve_reading build_curve(const pillar_history& by_term,
    std::size_t max_history) {
    service::curve_reading reading;

    std::set<std::int64_t> times;
    auto complete_from = std::numeric_limits<std::int64_t>::min();
    for (const auto& [tenor, points] : by_term) {
        if (points.empty())
            continue;
        for (const auto& point : points)
            times.insert(point.first);
        complete_from = std::max(complete_from, points.begin()->first);
    }
    if (times.empty())
        return reading;

    std::vector<std::int64_t> instants(times.lower_bound(complete_from),
        times.end());
    if (instants.size() > max_history)
        instants.erase(instants.begin(),
            instants.end() - static_cast<std::ptrdiff_t>(max_history));
    if (instants.empty())
        return reading;

    /*
     * The pillars are put in drawing order once, here, on the read's own
     * thread: the plot draws a curve per instant along this axis, and a frame
     * that sorted tenors would pay for it once per window instead of once per
     * read.
     */
    std::vector<std::string> pillars;
    for (const auto& [tenor, points] : by_term)
        if (!points.empty())
            pillars.push_back(tenor);
    const auto order = model::state::curve_table::pillar_order(pillars);
    reading.pillars.reserve(order.size());
    for (const auto at : order)
        reading.pillars.push_back(pillars[at]);

    const auto count = reading.pillars.size();
    reading.as_of_ns = instants;
    reading.values.assign(instants.size() * count, curve_gap());
    for (std::size_t row = 0; row < instants.size(); ++row) {
        const auto at = instants[row];
        for (std::size_t pillar = 0; pillar < count; ++pillar) {
            const auto& points = by_term.at(reading.pillars[pillar]);
            const auto value = points.upper_bound(at);
            if (value == points.begin())
                continue;
            reading.values[row * count + pillar] = std::prev(value)->second;
        }
    }
    return reading;
}

void run_curve_read(hedgr::session::transport::session_link& link,
    const curve_request& request, model::event::event& answer) {
    if (request.published) {
        const auto sets = link.published_curve_sets(request.key,
            curve_instants);
        if (!sets) {
            answer.message = sets.error().message.empty()
                ? sets.error().detail : sets.error().message;
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The published curve '" << request.key << "' failed: "
                << answer.message;
            return;
        }
        auto reading = service::read_published_curves(*sets);
        if (reading.pillars.empty()) {
            answer.message = "the deployment published no pillar for this curve";
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The published curve '" << request.key
                << "' answered " << sets->size() << " sets and no pillar";
            return;
        }
        if (!request.label.empty())
            reading.label = request.label;
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Published curve '" << request.key << "' (" << reading.label
            << "): " << reading.pillars.size() << " pillars, "
            << reading.as_of_ns.size() << " instants";
        answer.curve_label = std::move(reading.label);
        answer.curve_pillars = std::move(reading.pillars);
        answer.curve_as_of_ns = std::move(reading.as_of_ns);
        answer.curve_values = std::move(reading.values);
        return;
    }

    /*
     * A curve assembled from ticks is the object as it stood at each instant.
     * A pillar may be published at a slightly different time from its
     * neighbours, so each tenor's last value is carried forward to every
     * instant by build_curve.
     *
     * The newest max_history points per pillar are enough. An instant among
     * the newest max_history overall has fewer than max_history newer
     * instants, so each pillar's value at it is within that tail.
     */
    constexpr std::size_t max_history = model::state::curve_history_limit;
    pillar_history by_term;
    const auto count = std::min(request.member_ids.size(),
        request.member_tenors.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto read =
            link.market_series_tail(request.member_ids[i], max_history);
        if (!read) {
            /*
             * A failed pillar is not a curve with no observations. Saying so
             * keeps an expired session, a refused read and a genuinely empty
             * series from all reading as the same thing.
             */
            answer.message = read.error().message.empty()
                ? read.error().detail : read.error().message;
            return;
        }
        /*
         * A member named by a tenor is one series of that tenor, so its first
         * coordinate is the ladder's rung. A member named by nothing is a
         * series whose observations carry the ladder themselves - one
         * coordinate per term - and a read that kept only the first coordinate
         * would answer with one tenor, which is one point on the plot.
         */
        const auto& held = request.member_tenors[i].empty()
            ? read->terms : read->points;
        const auto tail = held.size() > max_history
            ? held.size() - max_history : std::size_t{0};
        for (const auto& point : std::span(held).subspan(tail)) {
            const auto tenor = request.member_tenors[i].empty()
                ? point.term : request.member_tenors[i];
            by_term[tenor][point.at_ns] = point.value;
        }
    }

    auto reading = build_curve(by_term, max_history);
    if (reading.pillars.empty()) {
        answer.message = "the curve's tenors hold no observations";
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The curve '" << request.key << "' has " << count
            << " members and no observation";
        return;
    }
    if (!request.label.empty())
        reading.label = request.label;
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Curve '" << request.key << "' from " << count << " members ("
        << reading.label << "): " << reading.pillars.size() << " pillars, "
        << reading.as_of_ns.size() << " instants";
    answer.curve_label = std::move(reading.label);
    answer.curve_pillars = std::move(reading.pillars);
    answer.curve_as_of_ns = std::move(reading.as_of_ns);
    answer.curve_values = std::move(reading.values);
}

}
