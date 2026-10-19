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
#include "hedgr.runtime/service/password_policy_service.hpp"
#include "hedgr.runtime/worker/policy_worker.hpp"
#include <memory>
#include <utility>

namespace hedgr::runtime::service {

namespace {

namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

}

password_policy_service::password_policy_service(
    const transport::session_options& seed_options,
    boost::asio::io_context& pool, model::command::event_queue& events)
    : seed_options_(seed_options), pool_(pool), events_(events) {
}

void password_policy_service::fetch_password_policy() {
    /*
     * The one place that builds options from the seed alone, because there is
     * no row to read: the create prompt is drawn while the store is still shut,
     * so the deployment the process was pointed at is the only one that can be
     * asked. It is the right one to ask, since a first run seeds its single
     * instance from exactly this description.
     */
    transport::session_options options;
    options.nats_url = seed_options_.nats_url;
    options.subject_prefix = seed_options_.subject_prefix;
    options.wire_format = seed_options_.wire_format;
    options.tls_ca = seed_options_.tls_ca;
    options.tls_cert = seed_options_.tls_cert;
    options.tls_key = seed_options_.tls_key;
    options.request_timeout = seed_options_.request_timeout;
    options.connect_timeout = seed_options_.connect_timeout;

    worker::policy_job job;
    job.options = std::move(options);
    job.subject_prefix = seed_options_.subject_prefix;

    /*
     * A deployment nobody pointed this process at cannot be asked. The state
     * is left stating that there is no policy rather than that one is coming,
     * because waiting for an answer that was never requested is a lie the
     * create prompt would tell for the rest of the run.
     */
    if (job.options.nats_url.empty()) {
        password_policy_.fail("no deployment is configured to ask");
        policy_requested_ = true;
        return;
    }

    password_policy_.begin();
    policy_read_ = std::make_shared<std::stop_source>();
    worker::spawn_policy_read(pool_, std::move(job), events_,
        policy_read_);
    policy_requested_ = true;
}

void password_policy_service::apply_password_policy(const event::event& ev) {
    policy_read_.reset();

    if (!ev.policy_available) {
        /*
         * The deployment did not state a policy, so nothing is assessed and
         * the create prompt says so. The alternative -- treating the absence
         * as a policy that requires nothing -- would turn an outage into a
         * screen that promises a password the deployment was about to refuse.
         */
        password_policy_.fail(ev.message);
        return;
    }

    password_policy_.set(ev.policy);
}

void password_policy_service::cancel() noexcept {
    if (policy_read_)
        policy_read_->request_stop();
    policy_read_.reset();
}

}
