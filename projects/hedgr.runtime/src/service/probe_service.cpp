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
#include "hedgr.runtime/service/probe_service.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.runtime/worker/probe_worker.hpp"
#include "support.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::runtime::service {

namespace {

namespace command = hedgr::model::command;
namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace model_state = hedgr::model::state;
namespace signal = hedgr::model::signal;
namespace transport = hedgr::session::transport;

using condition_source = model_state::condition_source;
using support::lg;
using support::steady_now_ns;

/**
 * @brief How often an instance is probed when the last probe answered.
 *
 * A few seconds is a glance rather than a gate: often enough that the chart the
 * Instance Manager draws has a shape to show and a status is never stale when
 * somebody looks at it, rare enough that polling every instance costs the
 * deployment nothing worth measuring.
 */
constexpr std::chrono::milliseconds probe_cadence{5000};

/**
 * @brief The longest an unreachable instance waits between attempts.
 *
 * A deployment that is down is not brought back by being asked faster, and a
 * fixed retry against twenty unreachable instances is the retry storm the
 * backoff exists to prevent.
 */
constexpr std::chrono::milliseconds probe_cadence_max{60000};

}

probe_service::probe_service(const model_state::instance_table& instances,
    boost::asio::io_context& pool, model::command::event_queue& events)
    : instances_(instances), pool_(pool), events_(events) {
}

std::optional<refusal>
probe_service::do_probe_instance(const command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Probe requested for '" << cmd.instance_id << "'";
    /*
     * Forcing a probe moves the deadline rather than starting the probe here:
     * the schedule is then the one place that decides whether an instance is
     * due, and an instance that is already being probed is not probed a second
     * time by an operator who pressed the button twice.
     */
    const auto handle = instances_.find(cmd.instance_id);
    if (!handle) {
        return refusal{{condition_source::probe, cmd.instance_id},
            std::format("cannot probe: no instance {}", cmd.instance_id)};
    }

    probes_.set_next_probe_ns(*handle, 0);
    return std::nullopt;
}

void probe_service::do_probe_all() {
    /*
     * Every instance, including the ones whose probe is in flight: moving a
     * deadline is always safe, and the in-flight check in pump_probes() is
     * what stops a sweep from doubling up on an instance.
     */
    const auto& layout = instances_.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        probes_.set_next_probe_ns(
            model_state::instance_handle{index, layout.generation[index]}, 0);
    }
}

void probe_service::pump_probes(bool started, const endpoint_for& endpoint,
    const probe_allowed& allowed) {
    /*
     * A stopped manager starts no probes: shutting down cancels every probe it
     * has, and a pump that outlives the shutdown command must not start a new
     * one behind it.
     */
    if (!started)
        return;

    const auto now = steady_now_ns();
    const auto& layout = instances_.layout();
    const auto rows = instances_.rows();

    bool swept = false;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;

        const auto& row = rows[index];
        if (probing_.contains(row.id))
            continue;

        const model_state::instance_handle handle{index, layout.generation[index]};
        if (probes_.next_probe_ns(handle) > now)
            continue;
        if (!allowed(row))
            continue;

        spawn_probe_for(row, endpoint);
        swept = true;
    }

    /*
     * One sample per sweep and not one per probe, because the chart plots the
     * estate once a poll rather than once an answer. The counts are taken as
     * the sweep starts, so a bar shows the state the previous sweep left the
     * estate in, and an instance removed since then is gone from the total
     * because the total is read here rather than remembered.
     */
    if (swept)
        probes_.record_snapshot(now, instances_.size());
}

void probe_service::spawn_probe_for(const domain::instance& row,
    const endpoint_for& endpoint) {
    /*
     * Built by the same call a session builds its own options with, because a
     * probe asks exactly the question a session asks first -- can this client
     * reach this deployment -- and an instance whose certificate path is wrong
     * is unreachable from here for the same reason it is unreachable there.
     * The probe is still not a session: what it borrows is the description of
     * how to reach the deployment, and the client that uses it is its own.
     */
    transport::session_options options =
        endpoint(row);

    worker::probe_job job;
    job.options = std::move(options);
    job.instance = row;

    auto source = std::make_shared<std::stop_source>();
    probing_[row.id] = source;

    /*
     * The deadline is left where it is while the probe is in flight. It is the
     * answer that moves it, and an instance whose probe never answers is held
     * back by the in-flight entry rather than by a deadline nobody ever
     * clears: a deadline pushed forward on spawn would quietly retire an
     * instance from the schedule if the probe were lost.
     */
    worker::spawn_probe(pool_, std::move(job), events_,
        std::move(source));
}

void probe_service::apply_probe_result(const event::event& ev) {
    /*
     * The probe is no longer in flight whatever happens next, and leaving the
     * entry behind would retire the instance from the schedule for the rest of
     * the run.
     */
    probing_.erase(ev.instance_id);

    /*
     * An instance that has gone since the probe started has nowhere to put the
     * result, and the handles of the table have moved on. The event is dropped
     * rather than reported: a fact about a removed instance is a fact about
     * nothing, and its slot now belongs to somebody else.
     */
    const auto handle = instances_.find(ev.instance_id);
    if (!handle)
        return;

    /*
     * The previous answer is read before the new one overwrites it, because the
     * row is the only place it lives and the signal is about the change rather
     * than about the state. A first result has nothing to differ from and
     * raises nothing, so a process that starts with six unreachable
     * deployments does not announce six faults it has only just learned about.
     */
    const domain::probe_result* previous = probes_.get(*handle);
    const bool had_previous = previous != nullptr;
    const auto previous_reach = had_previous
        ? previous->reach
        : domain::reachability::unknown;
    const auto previous_ready = had_previous
        ? previous->ready
        : domain::readiness::unknown;

    domain::probe_result result;
    result.reach = ev.reach;
    result.ready = ev.ready;
    result.latency_us = ev.latency_us;
    result.observed_at_ns = steady_now_ns();
    result.detail = ev.detail;
    /*
     * The flag is what keeps a deployment that answered from wearing the
     * defaults of one that never did: only a probe that reached the read
     * carries a structured answer, and everything else is the sentence in
     * detail.
     */
    if (ev.bootstrap_answered)
        result.bootstrap = ev.bootstrap;
    probes_.set(*handle, std::move(result));

    /*
     * The change, if there was one, is raised as a named signal rather than
     * acted on here. The sound and any later subscriber read the same name, and
     * the runtime stays ignorant of both, which is the point of the name being
     * the whole interface. Reachability is raised in preference to readiness
     * when both moved, because a broker that stopped answering also stops
     * reporting readiness and the operator would otherwise hear two faults for
     * one fact.
     */
    if (had_previous) {
        const domain::instance* row = instances_.get(*handle);
        const std::string subject = row != nullptr ? row->name : ev.instance_id;
        const std::int64_t at_ns = steady_now_ns();

        auto announce = [&](std::string_view name, std::string detail) {
            signal::signal value;
            value.name = std::string(name);
            value.detail = std::move(detail);
            value.at_ns = at_ns;
            signal::bus().raise(std::move(value));
        };

        if (ev.reach != previous_reach) {
            if (ev.reach == domain::reachability::reachable)
                announce(signal::names::instance_reachability_restored,
                    std::format("{} answers its broker again", subject));
            else
                announce(signal::names::instance_reachability_lost,
                    std::format("{} no longer answers its broker", subject));
        } else if (ev.reach == domain::reachability::reachable
            && ev.ready != previous_ready) {
            if (ev.ready == domain::readiness::ready)
                announce(signal::names::instance_readiness_recovered,
                    std::format("{} is ready", subject));
            else if (ev.ready == domain::readiness::bootstrapping)
                announce(signal::names::instance_readiness_bootstrapping,
                    std::format("{} is bootstrapping", subject));
            else
                announce(signal::names::instance_readiness_degraded,
                    std::format("{} stopped being ready and is now {}",
                        subject, domain::to_string(ev.ready)));
        }
    }

    /*
     * A probe that got no answer backs the next attempt off; a probe that got
     * an answer -- whatever that answer was -- returns to the cadence. A
     * deployment that is up but not bootstrapped is up, and polling it every
     * five seconds is how the operator finds out the moment it is set up.
     */
    auto& interval = probe_intervals_[ev.instance_id];
    if (ev.reach == domain::reachability::reachable)
        interval = probe_cadence;
    else
        interval = std::min(interval.count() == 0
                ? probe_cadence * 2
                : interval * 2,
            probe_cadence_max);

    probes_.set_next_probe_ns(*handle,
        steady_now_ns() +
            std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count());
}

void probe_service::forget_probe(model_state::instance_handle handle,
    const std::string& instance_id) {
    if (const auto found = probing_.find(instance_id); found != probing_.end()) {
        if (found->second)
            found->second->request_stop();
        probing_.erase(found);
    }
    probe_intervals_.erase(instance_id);
    probes_.forget(handle);
}

void probe_service::cancel() noexcept {
    for (auto& [instance_id, source] : probing_) {
        if (source)
            source->request_stop();
    }
    probing_.clear();
}

}
