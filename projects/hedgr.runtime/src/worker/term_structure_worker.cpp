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
#include "hedgr.runtime/worker/term_structure_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <cstdint>
#include <utility>

namespace hedgr::runtime::worker {

std::vector<model::domain::term_curve>
build_term_curves(const pillar_history& by_term, std::size_t max_history) {
    std::set<std::int64_t> times;
    auto complete_from = std::numeric_limits<std::int64_t>::min();
    for (const auto& [tenor, points] : by_term) {
        for (const auto& point : points)
            times.insert(point.first);
        complete_from = std::max(complete_from, points.begin()->first);
    }

    std::vector<std::int64_t> instants(times.lower_bound(complete_from),
        times.end());
    if (instants.size() > max_history)
        instants.erase(instants.begin(),
            instants.end() - static_cast<std::ptrdiff_t>(max_history));

    std::vector<model::domain::term_curve> curves;
    for (const auto at : instants) {
        model::domain::term_curve curve;
        curve.at_ns = at;
        for (const auto& [tenor, points] : by_term) {
            const auto value = std::prev(points.upper_bound(at))->second;
            curve.nodes.push_back(model::domain::term_node{
                .tenor = tenor, .value = value, .at_ns = at});
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

void run_term_structure_read(session::transport::session_link& link,
    std::span<const std::string> ids, std::span<const std::string> tenors,
    model::event::event& answer) {
    /*
     * A curve is the object as it stood at an instant. A pillar may be
     * published at a slightly different time from its neighbours, so the
     * curves are rebuilt by carrying each term's last value forward to
     * every instant: the newest curve is complete, and the history is a
     * continuous curve at each step rather than a handful of segments.
     *
     * The newest max_history points per pillar are enough. An instant
     * among the newest max_history overall has fewer than max_history
     * newer instants, so each pillar's value at it is within that tail.
     * The read is of a span of ticks, so the tail is cut here.
     */
    constexpr std::uint32_t max_history = 20;
    pillar_history by_term;
    for (std::size_t i = 0; i < ids.size() && i < tenors.size(); ++i) {
        const auto read = link.market_series_tail(ids[i], max_history);
        if (!read) {
            /*
             * A failed pillar is not a curve with no observations. Saying
             * so keeps an expired session, a refused read and a genuinely
             * empty series from all reading as the same thing.
             */
            answer.message = read.error().message.empty()
                ? read.error().detail : read.error().message;
            return;
        }
        const auto tail = read->points.size() > max_history
            ? read->points.size() - max_history : std::size_t{0};
        for (const auto& point : std::span(read->points).subspan(tail)) {
            const auto tenor = tenors[i].empty() ? point.term : tenors[i];
            by_term[tenor][point.at_ns] = point.value;
        }
    }

    answer.term_curves = build_term_curves(by_term, max_history);
    if (answer.term_curves.empty())
        answer.message = "the curve's tenors hold no observations";
}

}
