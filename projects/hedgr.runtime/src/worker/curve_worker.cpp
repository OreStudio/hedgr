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

/**
 * @brief How many calendar days of observations a ladder's read spans.
 *
 * An observation window is bounded by days and ends today, and the synthetic
 * feed writes one ladder a day, so this is also how many curves the plot can
 * overlay. The newest instant is the one a desk reads.
 */
constexpr int curve_observation_days = 5;

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

    /*
     * A member with no tenor of its own is one series that carries the whole
     * ladder, and the deployment states that shape rather than leaving it to be
     * inferred: the evolution read names the object's axes and every node on
     * them, then one value per node per instant. A tick reply names a coordinate
     * and not a term, so reading one as ticks answers with the series' index
     * tenor - one point on the plot.
     */
    if (!request.member_identity.empty()) {
        const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                .count();
        const auto from_ns = now_ns
            - static_cast<std::int64_t>(curve_observation_days) * 24 * 60 * 60
                * 1'000'000'000;
        const auto read = link.market_series_evolution(
            request.member_ids.empty() ? std::string_view{}
                                       : request.member_ids.front(),
            request.member_identity, from_ns, now_ns);
        if (!read) {
            answer.message = read.error().message.empty()
                ? read.error().detail : read.error().message;
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The evolution of curve '" << request.key
                << "' failed: " << answer.message;
            return;
        }

        /*
         * The pillar of a node is the label its term axis carries. An object
         * with no term axis has no ladder to plot, and says so rather than
         * drawing every node's labels joined into one name.
         */
        std::size_t term_field = read->fields.size();
        for (std::size_t i = 0; i < read->fields.size(); ++i)
            if (read->fields[i] == "term")
                term_field = i;
        if (term_field == read->fields.size() || read->nodes.empty()
            || read->instants.empty()) {
            answer.message = std::format(
                "the deployment states no term axis for this curve ({} axes, "
                "{} nodes, {} instants)",
                read->fields.size(), read->nodes.size(),
                read->instants.size());
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Curve '" << request.key << "': " << answer.message;
            return;
        }

        service::curve_reading reading;
        reading.pillars.reserve(read->nodes.size());
        for (const auto& node : read->nodes) {
            if (term_field >= node.size())
                continue;
            reading.pillars.push_back(node[term_field]);
        }
        const auto count = reading.pillars.size();
        reading.as_of_ns.reserve(read->instants.size());
        reading.values.reserve(read->instants.size() * count);
        for (const auto& instant : read->instants) {
            reading.as_of_ns.push_back(instant.as_of_ns);
            for (std::size_t i = 0; i < count; ++i)
                reading.values.push_back(i < instant.values.size()
                        ? instant.values[i] : curve_gap());
        }
        if (!request.label.empty())
            reading.label = request.label;
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Curve '" << request.key << "' from the deployment's own shape ("
            << reading.label << "): " << reading.pillars.size()
            << " pillars, " << reading.as_of_ns.size() << " instants, axes '"
            << (read->fields.empty() ? std::string{} : read->fields.front())
            << "'";
        answer.curve_label = std::move(reading.label);
        answer.curve_pillars = std::move(reading.pillars);
        answer.curve_as_of_ns = std::move(reading.as_of_ns);
        answer.curve_values = std::move(reading.values);
        return;
    }

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
