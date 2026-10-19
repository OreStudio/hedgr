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
#include <map>
#include <set>
#include <cstdint>
#include <utility>

namespace hedgr::runtime::worker {

void spawn_term_structure_read(boost::asio::io_context& context,
    term_structure_job job, model::command::event_queue& events) {
    const auto key = job.key;
    const auto label = job.label;
    const auto ids = job.member_ids;
    const auto tenors = job.member_tenors;

    model::event::event fact;
    fact.kind = model::event::event_kind::term_structure;
    fact.instance_id = job.session.instance_id;
    fact.account_id = job.session.account_id;
    fact.curve_key = key;
    fact.curve_label = label;

    auto work = [ids, tenors](session::transport::session_client& client,
                    model::event::event& answer) {
        /*
         * A curve is the object as it stood at an instant. A pillar may be
         * published at a slightly different time from its neighbours, so the
         * curves are rebuilt by carrying each term's last value forward to
         * every instant: the newest curve is complete, and the history is a
         * continuous curve at each step rather than a handful of segments.
         */
        std::map<std::string, std::map<std::int64_t, double>> by_term;
        std::set<std::int64_t> times;
        for (std::size_t i = 0; i < ids.size() && i < tenors.size(); ++i) {
            const auto read = client.market_series_points(ids[i], 10000);
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
            for (const auto& point : read->points) {
                const auto tenor = tenors[i].empty() ? point.term : tenors[i];
                by_term[tenor][point.at_ns] = point.value;
                times.insert(point.at_ns);
            }
        }

        constexpr std::size_t max_history = 20;
        std::vector<std::int64_t> instants(times.begin(), times.end());
        if (instants.size() > max_history)
            instants.erase(instants.begin(),
                instants.end() - static_cast<std::ptrdiff_t>(max_history));

        for (const auto at : instants) {
            model::domain::term_curve curve;
            curve.at_ns = at;
            for (const auto& [tenor, points] : by_term) {
                const auto found = points.upper_bound(at);
                if (found == points.begin())
                    continue;
                const auto value = std::prev(found)->second;
                curve.nodes.push_back(model::domain::term_node{
                    .tenor = tenor, .value = value, .at_ns = at});
            }
            if (!curve.nodes.empty())
                answer.term_curves.push_back(std::move(curve));
        }
        if (answer.term_curves.empty())
            answer.message = "the curve's tenors hold no observations";
    };
    boost::asio::co_spawn(context,
        run_session_read(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}

}
