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
#ifndef HEDGR_MODEL_SIGNAL_SIGNAL_HPP
#define HEDGR_MODEL_SIGNAL_SIGNAL_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hedgr::model::signal {

/**
 * @brief A fact with a name, raised by any part of the application.
 *
 * This is a separate thing from a model event. An event is a closed kind from a
 * worker to the frame loop, and it exists to keep the state tables
 * single-writer; a signal is an open name that any code may raise and that
 * anything may listen for, with the name as the whole interface. See ADR 8.
 *
 * The detail sentence travels with the signal rather than being reconstructed
 * by each listener, because a listener that had to build it would need the
 * subject of the signal and every listener would describe the same fact
 * differently.
 */
struct signal final {
    /** @brief What happened, as a dotted lowercase name. */
    std::string name;

    /** @brief The human sentence, for a status bar or a log. */
    std::string detail;

    /** @brief When it was raised, in nanoseconds on the steady clock. */
    std::int64_t at_ns = 0;
};

/**
 * @brief The channel every signal is carried on, drained once per frame.
 *
 * The bus is deliberately tiny. It holds a mutex, a vector and nothing else,
 * because the job is to hand a signal from whoever raised it to the frame loop
 * without the raiser knowing who listens. A subscription lifetime, a topic tree
 * or a delivery guarantee would each be a framework to maintain, and none of
 * them is needed to play a sound or fill a status bar.
 *
 * Threads are the reason for the mutex. A worker may raise at any moment and
 * the frame loop drains at the top of a frame, so the swap is the only thing
 * that has to be atomic. Nothing else is shared.
 */
class HEDGR_MODEL_EXPORT signal_bus final {
public:
    /** @brief Adds @p value to the inbox of the next drain. */
    void raise(signal value);

    /**
     * @brief Takes every signal raised since the last drain, oldest first.
     *
     * The bus is left empty, so a signal is delivered to the frame loop exactly
     * once. A signal raised while the drain is taking the current batch belongs
     * to the next one, because the two never see the same vector.
     */
    [[nodiscard]] std::vector<signal> drain();

    /** @brief Whether anything is waiting for a drain. */
    [[nodiscard]] bool empty() const;

private:
    mutable std::mutex mutex_;
    std::vector<signal> raised_;
};

/**
 * @brief The process's bus.
 *
 * One bus rather than one per owner, because the whole point of a signal is
 * that the raiser does not hold the listener. A deployment that stops being
 * ready is raised by the runtime, which is a long way from the interface that
 * plays a sound, and threading a bus through every layer between them would put
 * the coupling back that the name was meant to remove.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT signal_bus& bus();

}

#endif
