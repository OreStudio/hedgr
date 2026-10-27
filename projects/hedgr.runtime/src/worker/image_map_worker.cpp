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
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include <utility>

namespace hedgr::runtime::worker {

void run_image_map_read(session::transport::session_link& link,
    model::event::event& answer) {
    if (const auto images = link.currency_images(); images) {
        for (const auto& image : *images) {
            answer.image_refs.push_back(
                {.currency = image.currency, .image_id = image.image_id});
        }
    } else {
        answer.message = images.error().message;
    }
}

void spawn_image_map_read(boost::asio::io_context& context, image_map_job job,
    model::command::event_queue& events) {
    model::event::event fact;
    fact.kind = model::event::event_kind::currency_image_map;
    fact.instance_id = job.session.instance_id;
    fact.account_id = job.session.account_id;
    fact.tenant_id = job.session.tenant_id;

    auto work = [](session::transport::session_link& link,
                    model::event::event& answer) {
        run_image_map_read(link, answer);
    };
    boost::asio::co_spawn(context,
        run_session_read_on_link(std::move(job.session), std::move(fact),
            std::move(work), events),
        boost::asio::detached);
}

}
