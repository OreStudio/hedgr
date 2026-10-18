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
#include "hedgr.runtime/worker/crm_worker.hpp"
#include <chrono>
#include "hedgr.model/event/event.hpp"
#include <algorithm>
#include <utility>
namespace hedgr::runtime::worker {
void spawn_crm_read(boost::asio::io_context& context, crm_job job,
    model::command::event_queue& events) {
    const auto party = job.party_id;
    const auto crm_name = job.crm_name;
    const auto reciprocal = job.reciprocal;
    model::event::event fact;
    fact.kind = model::event::event_kind::crm_rates;
    fact.instance_id = job.session.instance_id;
    fact.account_id = job.session.account_id;
    fact.tenant_id = job.session.tenant_id;
    fact.party_id = party;
    fact.crm_name = crm_name;
    fact.crm_reciprocal = reciprocal;
    auto work = [party, crm_name, reciprocal](
                    session::transport::session_client& client,
                    model::event::event& answer) {
        const auto started = std::chrono::steady_clock::now();
        const auto rates = client.crm_rates(party, crm_name, reciprocal);
        answer.crm_latency_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        if (!rates) {
            answer.message = rates.error().message;
            return;
        }
        std::string newest;
        for (const auto& item : *rates) {
            answer.crm_cells.push_back(model::domain::crm_cell{
                .crm_name = item.crm_name,
                .base_currency_code = item.base_currency_code,
                .quote_currency_code = item.quote_currency_code,
                .rate = item.rate,
                .status = item.status,
                .as_of = item.as_of,
                .reciprocal = item.reciprocal,
                .delta_pct = item.delta_pct});
            newest = std::max(newest, item.as_of);
        }
        answer.crm_as_of = std::move(newest);
        if (const auto conventions = client.currency_pair_conventions()) {
            for (const auto& item : *conventions) {
                answer.crm_conventions.push_back(model::domain::crm_convention{
                    .pair_code = item.pair_code,
                    .decimal_places = item.decimal_places,
                    .pip_factor = item.pip_factor,
                    .tick_size = item.tick_size});
            }
        }
    };
    boost::asio::co_spawn(context,
        run_session_read(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}
}
