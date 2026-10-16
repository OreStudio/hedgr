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
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace hedgr::runtime::worker {

std::string failure_detail(const session::transport::session_error& error) {
    using enum session::transport::session_errc;

    /*
     * The headline names the thing that is not working and nothing more. The
     * broker and the service behind it are the two failures a reader acts on
     * differently, so each gets its own two words; every other code already
     * carries a short sentence of its own. The wire's text follows on its own
     * line, unparaphrased, because that is the part that goes into a ticket.
     */
    if (error.code == unreachable)
        return error.detail.empty()
            ? std::string("NATS is down")
            : std::format("NATS is down\n{}", error.detail);
    if (error.code == services_unavailable)
        return error.detail.empty()
            ? std::string("IAM is down")
            : std::format("IAM is down\n{}", error.detail);

    return error.detail.empty()
        ? error.message
        : std::format("{}\n{}", error.message, error.detail);
}

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

using hedgr::model::command::event_queue;

/** @brief How long the attempt has taken so far, in microseconds. */
[[nodiscard]] std::int64_t elapsed_us(
    std::chrono::steady_clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started)
        .count();
}

/**
 * @brief The deployment's own bootstrap answer, as the model holds it.
 *
 * The three fields move across unchanged; the only decision made here is that
 * a version the deployment did not state stays absent rather than becoming an
 * empty string, because the pane draws a row only for a fact that is there.
 * The word "unknown" the old flat sentence wrote in its place was a value the
 * deployment never sent.
 */
[[nodiscard]] domain::bootstrap_status answer_from(
    const transport::bootstrap_status& status) {
    domain::bootstrap_status answer;
    answer.in_bootstrap_mode = status.is_in_bootstrap_mode;
    answer.has_tenant = status.has_tenant;
    if (!status.version.empty())
        answer.version = status.version;
    return answer;
}

/** @brief Posts one probe's outcome, tagged with the instance it describes. */
void push_result(event_queue& events, const domain::instance& instance,
    domain::reachability reach, domain::readiness ready,
    std::int64_t latency_us, std::string detail,
    std::optional<domain::bootstrap_status> bootstrap = std::nullopt) {
    event::event fact;
    fact.kind = event::event_kind::probe_result;
    fact.instance_id = instance.id;
    fact.reach = reach;
    fact.ready = ready;
    fact.latency_us = latency_us;
    fact.detail = std::move(detail);
    if (bootstrap) {
        /*
         * The flag is what says the fields below are the deployment's answer
         * rather than their defaults, because a deployment that answered false
         * and one that never answered both carry a false in the mode field.
         */
        fact.bootstrap_answered = true;
        fact.bootstrap = std::move(*bootstrap);
    }
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
            failure_detail(connected.error()));
        co_return;
    }

    const auto answer = client.bootstrap_status();

    if (stop->stop_requested())
        co_return;

    if (!answer) {
        /*
         * The broker answered, so the deployment is reachable whatever IAM
         * said: what failed is the service behind the broker, not the broker.
         * That is a fault rather than a gap, because a deployment that still
         * needs bootstrapping answers this read. It is service_missing and not
         * unknown, which is what a probe that has not reported yet leaves.
         */
        push_result(events, job.instance, domain::reachability::reachable,
            domain::readiness::service_missing, elapsed_us(started),
            failure_detail(answer.error()));
        co_return;
    }

    const auto& status = answer.value();
    push_result(events, job.instance, domain::reachability::reachable,
        status.is_in_bootstrap_mode ? domain::readiness::bootstrapping
                                    : domain::readiness::ready,
        elapsed_us(started), status.message, answer_from(status));
}

}

void spawn_probe(boost::asio::io_context& context, probe_job job,
    event_queue& events, std::shared_ptr<std::stop_source> stop) {
    boost::asio::co_spawn(context,
        run_probe(std::move(job), events, std::move(stop)),
        boost::asio::detached);
}

}
