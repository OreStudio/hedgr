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
#include "hedgr.runtime/worker/probe_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

using hedgr::model::command::event_queue;

/**
 * @brief The subject a probe asks, spelled once so a reader sees the question.
 *
 * The deployment-relative spelling, which is what the transport prefixes; the
 * full wire name is only ever built by the transport itself.
 */
constexpr std::string_view bootstrap_subject = "iam.v1.ops.bootstrap_status";

/** @brief How long the attempt has taken so far, in microseconds. */
[[nodiscard]] std::int64_t elapsed_us(
    std::chrono::steady_clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started)
        .count();
}

/**
 * @brief The sentence that explains a failure, for the tooltip.
 *
 * The low-level cause is the whole reason the field exists, so it is preferred
 * over the friendly message; a failure that named no cause falls back to the
 * message rather than leaving an empty box where the reason should be.
 */
[[nodiscard]] std::string cause(const transport::session_error& error) {
    return error.detail.empty() ? error.message : error.detail;
}

/**
 * @brief What a deployment said about itself, as one line.
 *
 * Stated in the deployment's own field names, because the reader of a tooltip
 * is usually the person who will go and look at that deployment.
 */
[[nodiscard]] std::string describe(const transport::bootstrap_status& status) {
    auto detail = std::format(
        "{}: in bootstrap mode = {}, has tenant = {}, version = {}",
        bootstrap_subject, status.is_in_bootstrap_mode ? "true" : "false",
        status.has_tenant ? "true" : "false",
        status.version.empty() ? "unknown" : status.version);
    if (!status.message.empty())
        detail.append(std::format("; {}", status.message));
    return detail;
}

/** @brief Posts one probe's outcome, tagged with the instance it describes. */
void push_result(event_queue& events, const domain::instance& instance,
    domain::reachability reach, domain::readiness ready,
    std::int64_t latency_us, std::string detail) {
    event::event fact;
    fact.kind = event::event_kind::probe_result;
    fact.instance_id = instance.id;
    fact.reach = reach;
    fact.ready = ready;
    fact.latency_us = latency_us;
    fact.detail = std::move(detail);
    events.push(std::move(fact));
}

/**
 * @brief One probe, start to finish, reporting exactly once.
 *
 * Written as a named coroutine rather than a lambda on purpose: clang elides
 * the frame of a lambda coroutine onto the caller's stack even when the
 * awaitable outlives the call, and the pool thread then resumes a frame whose
 * stack slot is gone.
 */
boost::asio::awaitable<void> run_probe(probe_job job, event_queue& events,
    std::shared_ptr<std::stop_source> stop) {
    /*
     * The spawn that reaches this coroutine may have come from the frame loop,
     * so the first thing a probe does is hand itself to the probe pool. Every
     * call below blocks, and blocking the frame loop is the one thing the
     * concurrency contract forbids.
     */
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    if (stop->stop_requested())
        co_return;

    const auto started = std::chrono::steady_clock::now();

    /*
     * The client is scoped to this one attempt and destroyed on every way out
     * of it, which is what makes "no probe shares a connection" true by
     * construction rather than by convention.
     */
    transport::session_client client(job.options);
    const auto connected = client.connect();

    /*
     * The second cancellation check, after the blocking call: a probe whose
     * instance was removed while the connect was in flight reports nothing,
     * because the row that would take the answer is already gone and a fact
     * about a removed instance is not a fact about anything.
     */
    if (stop->stop_requested())
        co_return;

    if (!connected) {
        /*
         * No connection was opened, so the broker is unreachable whatever the
         * cause was: a refused address, an untrusted certificate and a
         * connect timeout are all the same answer to the question asked.
         */
        push_result(events, job.instance, domain::reachability::unreachable,
            domain::readiness::unknown, elapsed_us(started),
            cause(connected.error()));
        co_return;
    }

    const auto answer = client.bootstrap_status();

    if (stop->stop_requested())
        co_return;

    if (!answer) {
        /*
         * The broker answered, so the deployment is reachable whatever IAM
         * said: what failed is the service behind the broker, not the broker.
         * Readiness stays unknown, and unknown is deliberately not
         * bootstrapping -- a deployment whose IAM is down has not announced
         * that it is halfway through setting itself up, it has said nothing.
         */
        push_result(events, job.instance, domain::reachability::reachable,
            domain::readiness::unknown, elapsed_us(started),
            cause(answer.error()));
        co_return;
    }

    const auto& status = answer.value();
    push_result(events, job.instance, domain::reachability::reachable,
        status.is_in_bootstrap_mode ? domain::readiness::bootstrapping
                                    : domain::readiness::ready,
        elapsed_us(started), describe(status));
}

}

void spawn_probe(boost::asio::io_context& context, probe_job job,
    event_queue& events, std::shared_ptr<std::stop_source> stop) {
    boost::asio::co_spawn(context,
        run_probe(std::move(job), events, std::move(stop)),
        boost::asio::detached);
}

}
