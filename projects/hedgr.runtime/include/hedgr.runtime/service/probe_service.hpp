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
#ifndef HEDGR_RUNTIME_SERVICE_PROBE_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_PROBE_SERVICE_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/condition_table.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace hedgr::runtime::service {

/** @brief A request the schedule refused, as the condition it is raised as. */
struct refusal final {
    model::state::condition_key key;
    std::string message;
};

/**
 * @brief How to reach the deployment an instance row describes.
 *
 * Asked at the moment a probe is spawned rather than held, because the store
 * path it reads only exists once the manager has started.
 */
using endpoint_for = std::function<session::transport::session_options(
    const model::domain::instance&)>;

/**
 * @brief When each instance is probed next, which probes are in flight, and
 * what each last said.
 *
 * Reads the instance table and never writes it. Runs on the frame thread; the
 * probes themselves run on the pool it is handed and answer through the event
 * channel, so the probe table keeps the single writer the concurrency model
 * requires.
 */
class probe_service final {
public:
    probe_service(const model::state::instance_table& instances,
        boost::asio::io_context& pool, model::command::event_queue& events);

    /** @brief The probes' last results and next deadlines, per instance. */
    [[nodiscard]] const model::state::probe_table& probes() const noexcept {
        return probes_;
    }

    /** @brief Whether no probe is in flight. */
    [[nodiscard]] bool idle() const noexcept { return probing_.empty(); }

    [[nodiscard]] std::optional<refusal>
    do_probe_instance(const model::command::command& cmd);
    void do_probe_all();

    /**
     * @brief Spawns a probe for every instance whose deadline has passed.
     *
     * Run at the end of pump(), after the commands and the events have been
     * applied, so a probe asked for this frame or an answer that landed this
     * frame is already reflected in the deadline it reads. An instance with a
     * probe in flight is skipped: an instance is never probed twice at once,
     * because two probes racing on one instance would report two results for
     * one moment and would spend two of the pool's two threads doing it.
     */
    void pump_probes(bool started, const endpoint_for& endpoint);

    /** @brief Applies one probe's answer: the result, then the next deadline. */
    void apply_probe_result(const model::event::event& ev);

    /**
     * @brief Drops everything the schedule holds for one instance.
     *
     * Called when an instance is removed, so no deadline, no interval and no
     * running probe outlives the row they describe.
     */
    void forget_probe(model::state::instance_handle handle,
                      const std::string& instance_id);

    /** @brief Cancels every probe in flight and leaves the schedule alone. */
    void cancel() noexcept;

private:
    /** @brief Starts one probe, and remembers that it is in flight. */
    void spawn_probe_for(const model::domain::instance& row,
                         const endpoint_for& endpoint);

    const model::state::instance_table& instances_;
    boost::asio::io_context& pool_;
    model::command::event_queue& events_;
    model::state::probe_table probes_;

    /**
     * @brief The probes that are in flight, by the instance they measure.
     *
     * An entry is what stops an instance being probed twice at once, and the
     * stop source beside it is what cancels a probe whose instance has gone or
     * whose application is stopping.
     */
    std::unordered_map<std::string, std::shared_ptr<std::stop_source>> probing_;

    /**
     * @brief The cadence each instance currently polls on, by instance id.
     *
     * The deadline itself lives in the probe table, beside the result; this is
     * the other half of the same schedule, because doubling a backoff needs to
     * know what the previous one was. An instance with no entry is on the
     * default cadence, which is the state every instance starts in.
     */
    std::unordered_map<std::string, std::chrono::milliseconds> probe_intervals_;
};

}

#endif
