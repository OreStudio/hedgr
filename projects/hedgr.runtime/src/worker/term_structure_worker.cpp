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
         * The observations are one pillar per row; a curve is the rows sharing
         * one observation time. Group by time, then by term, so repeated rows
         * for the same pillar collapse and the last twenty curves are kept.
         */
        std::map<std::int64_t, std::map<std::string, double>> by_time;
        for (std::size_t i = 0; i < ids.size() && i < tenors.size(); ++i) {
            const auto read = client.market_series_points(ids[i], 10000);
            if (!read)
                continue;
            for (const auto& point : read->points) {
                const auto tenor = tenors[i].empty() ? point.term : tenors[i];
                by_time[point.at_ns][tenor] = point.value;
            }
        }

        constexpr std::size_t max_history = 20;
        const auto first = by_time.size() > max_history
            ? std::prev(by_time.end(), max_history) : by_time.begin();
        for (auto it = first; it != by_time.end(); ++it) {
            model::domain::term_curve curve;
            curve.at_ns = it->first;
            for (const auto& [tenor, value] : it->second) {
                curve.nodes.push_back(model::domain::term_node{
                    .tenor = tenor, .value = value, .at_ns = it->first});
            }
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
