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
#include "hedgr.runtime/service/session_service.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include "support.hpp"
#include <algorithm>
#include <utility>

namespace hedgr::runtime::service {

namespace {

namespace worker = hedgr::runtime::worker;

using support::running_key;

/** @brief The actor a process runs when no test has stood one in. */
[[nodiscard]] worker::tick_feed_factory real_feeds() {
    return [](worker::session_actor_options options)
        -> std::unique_ptr<worker::tick_feed> {
        auto actor =
            std::make_unique<worker::session_actor>(std::move(options));
        actor->start();
        return actor;
    };
}

}

session_service::session_service(model::command::event_queue& events,
    worker::tick_feed_factory feeds)
    : events_(events), feeds_(std::move(feeds)) {
    if (!feeds_)
        feeds_ = real_feeds();
}

std::shared_ptr<worker::tick_feed>
session_service::open(worker::session_actor_options options) {
    options.events = &events_;
    const auto key =
        running_key(options.key.instance_id, options.key.account_id);
    if (const auto found = actors_.find(key); found != actors_.end()) {
        found->second->request_stop();
        retiring_.push_back(std::move(found->second));
        actors_.erase(found);
    }

    std::shared_ptr<worker::tick_feed> actor(feeds_(std::move(options)));
    if (!actor)
        return actor;

    actors_.insert_or_assign(key, actor);
    return actor;
}

std::shared_ptr<worker::tick_feed> session_service::feed(
    std::string_view instance_id, std::string_view account_id) const {
    const auto found = actors_.find(running_key(instance_id, account_id));
    return found == actors_.end() ? nullptr : found->second;
}

bool session_service::submit_read(std::string_view instance_id,
    std::string_view account_id, worker::read_request request) {
    const auto actor = feed(instance_id, account_id);
    return actor != nullptr && actor->submit_read(std::move(request));
}

void session_service::retire(std::string_view instance_id,
    std::string_view account_id) {
    const auto found = actors_.find(running_key(instance_id, account_id));
    if (found == actors_.end())
        return;
    found->second->request_stop();
    retiring_.push_back(std::move(found->second));
    actors_.erase(found);
}

void session_service::cancel() {
    for (auto& [key, actor] : actors_) {
        static_cast<void>(key);
        actor->request_stop();
    }
    actors_.clear();

    /*
     * Dropping the last reference to an actor joins its thread, which is why
     * every one of them is asked to stop before any is destroyed.
     */
    for (auto& actor : retiring_)
        actor->request_stop();
    retiring_.clear();
}

void session_service::reap() {
    std::erase_if(retiring_,
        [](const auto& actor) { return actor->finished(); });
}

}
