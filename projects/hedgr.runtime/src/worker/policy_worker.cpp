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
#include "hedgr.runtime/worker/policy_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <format>
#include <string>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

using hedgr::model::command::event_queue;

/** @brief The subject asked, spelled once so a reader sees the question. */
constexpr std::string_view policy_subject = "iam.v1.ops.get_password_policy";

/**
 * @brief The sentence a person reads when the policy could not be read.
 *
 * The subject and the deployment are both in it, because the reader is usually
 * the person who will go and look at that deployment. The low-level cause is
 * preferred over the friendly message for the same reason a probe's tooltip
 * does: it is the only part that says what actually went wrong.
 */
[[nodiscard]] std::string cause(std::string_view subject_prefix,
    const transport::session_error& error) {
    const auto said = error.detail.empty() ? error.message : error.detail;
    return std::format("{} on {}: {}", policy_subject,
        subject_prefix.empty() ? "the deployment" : subject_prefix, said);
}

/**
 * @brief Reports the outcome, whichever way the attempt ended.
 *
 * Written once so the coroutine has one exit for a success, one for a refusal
 * and one for a loss, and so the event's shape is described in one place.
 */
void push_result(event_queue& events, bool available,
    domain::password_policy policy, std::string message) {
    event::event fact;
    fact.kind = event::event_kind::password_policy_fetched;
    fact.policy_available = available;
    fact.policy = std::move(policy);
    fact.message = std::move(message);
    events.push(std::move(fact));
}

/**
 * @brief One policy read, start to finish, reporting exactly once.
 *
 * A named coroutine rather than a lambda for the reason the probe worker gives:
 * clang elides a lambda coroutine's frame onto the caller's stack even when the
 * awaitable outlives the call, and the pool thread then resumes a frame whose
 * stack slot is gone.
 */
boost::asio::awaitable<void> run_policy_read(policy_job job,
    event_queue& events, std::shared_ptr<std::stop_source> stop) {
    /*
     * The spawn may have come from the frame loop, so the read hands itself to
     * the pool first. Every call below blocks.
     */
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    if (stop->stop_requested())
        co_return;

    /*
     * The client is scoped to this one attempt, so the read never shares a
     * connection with a probe or with an operator's session: a session is the
     * person's working round trip and a background read must not occupy it.
     */
    transport::session_client client(job.options);
    const auto connected = client.connect();

    if (stop->stop_requested())
        co_return;

    if (!connected) {
        push_result(events, false, domain::password_policy{},
            cause(job.subject_prefix, connected.error()));
        co_return;
    }

    const auto answer = client.password_policy();

    if (stop->stop_requested())
        co_return;

    if (!answer) {
        push_result(events, false, domain::password_policy{},
            cause(job.subject_prefix, answer.error()));
        co_return;
    }

    if (!answer->success) {
        /*
         * The deployment answered and refused. Its own message is the whole
         * explanation, so it is kept verbatim rather than wrapped: this is the
         * deployment talking about itself and a second voice would only blur
         * which of the two said it.
         */
        push_result(events, false, domain::password_policy{},
            answer->message.empty()
                ? std::format("{} on {}: the deployment refused the read",
                      policy_subject, job.subject_prefix)
                : answer->message);
        co_return;
    }

    /*
     * Copied field by field rather than moved as one, because the transport's
     * type and the model's are separate declarations: the transport owns the
     * deployment's spelling of a reply and the model owns what a screen reads.
     */
    push_result(events, true,
        domain::password_policy{
            .min_length = answer->min_length,
            .require_uppercase = answer->require_uppercase,
            .require_lowercase = answer->require_lowercase,
            .require_digit = answer->require_digit,
            .require_special = answer->require_special,
            .special_chars = answer->special_chars},
        {});
}

}

void spawn_policy_read(boost::asio::io_context& context, policy_job job,
    event_queue& events, std::shared_ptr<std::stop_source> stop) {
    boost::asio::co_spawn(context,
        run_policy_read(std::move(job), events, std::move(stop)),
        boost::asio::detached);
}

}
