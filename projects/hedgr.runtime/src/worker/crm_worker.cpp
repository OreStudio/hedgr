/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include "hedgr.runtime/worker/crm_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include <algorithm>
#include <utility>
namespace hedgr::runtime::worker {
void spawn_crm_read(boost::asio::io_context& context, crm_job job,
    model::command::event_queue& events) {
    const auto party = job.party_id;
    model::event::event fact;
    fact.kind = model::event::event_kind::crm_rates;
    fact.instance_id = job.session.instance_id;
    fact.account_id = job.session.account_id;
    fact.tenant_id = job.session.tenant_id;
    auto work = [party](session::transport::session_client& client,
                    model::event::event& answer) {
        const auto rates = client.crm_rates(party, "", false);
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
    };
    boost::asio::co_spawn(context,
        run_session_read(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}
}
