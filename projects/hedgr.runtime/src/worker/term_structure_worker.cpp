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
        std::int64_t newest = 0;
        for (std::size_t i = 0; i < ids.size() && i < tenors.size(); ++i) {
            const bool whole_curve = tenors[i].empty();
            const auto read = client.market_series_points(ids[i],
                whole_curve ? 500 : 1);
            if (!read || read->points.empty())
                continue;
            if (!whole_curve) {
                const auto& point = read->points.back();
                answer.term_nodes.push_back(model::domain::term_node{
                    .tenor = tenors[i],
                    .value = point.value,
                    .at_ns = point.at_ns});
                newest = std::max(newest, point.at_ns);
                continue;
            }
            std::map<std::string, model::domain::term_node> by_term;
            for (const auto& point : read->points) {
                by_term[point.term] = model::domain::term_node{
                    .tenor = point.term,
                    .value = point.value,
                    .at_ns = point.at_ns};
            }
            for (auto& [tenor, node] : by_term) {
                newest = std::max(newest, node.at_ns);
                answer.term_nodes.push_back(std::move(node));
            }
        }
        answer.observed_at_ns = newest;
        if (answer.term_nodes.empty())
            answer.message = "the curve's tenors hold no observations";
    };
    boost::asio::co_spawn(context,
        run_session_read(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}

}
