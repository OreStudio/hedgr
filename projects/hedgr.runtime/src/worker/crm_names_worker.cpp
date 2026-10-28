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
#include "hedgr.runtime/worker/crm_names_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include <utility>

namespace hedgr::runtime::worker {

void run_crm_names_read(session::transport::session_link& link,
    std::string_view party, model::event::event& answer) {
    const auto names = link.crm_names(party);
    if (!names) {
        answer.message = names.error().message;
        return;
    }
    answer.crm_names = *names;
}

void spawn_crm_names_read(boost::asio::io_context& context,
    crm_names_job job, model::command::event_queue& events) {
    const read_request request = crm_names_request{.party_id = job.party_id};
    auto fact = read_event(request,
        model::domain::session_key{job.session.instance_id,
            job.session.account_id},
        job.session.tenant_id);
    auto work = [request](session::transport::session_link& link,
                    model::event::event& answer) {
        run_read(link, request, answer);
    };
    boost::asio::co_spawn(context,
        run_session_read_on_link(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}

}
