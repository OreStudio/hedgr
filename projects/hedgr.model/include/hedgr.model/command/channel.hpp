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
#ifndef HEDGR_MODEL_COMMAND_CHANNEL_HPP
#define HEDGR_MODEL_COMMAND_CHANNEL_HPP

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/export.hpp"
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace hedgr::model::command {

/**
 * @brief A one-way hand-off of values between two threads.
 *
 * There are exactly two of these in the process: the interface pushes commands
 * and the runtime drains them, and the runtime pushes events and the interface
 * drains them. A single shared channel per direction is a real invariant
 * rather than incidental sharing, so access is serialized here and nowhere
 * else. The lock is held only for a swap.
 */
template <typename T>
class channel final {
public:
    /** @brief Buffers a value for the reader. */
    void push(T item) {
        const std::scoped_lock lock(mutex_);
        buffer_.push_back(std::move(item));
    }

    /**
     * @brief Takes everything buffered, leaving the channel empty.
     *
     * The reader drains once per frame and then works lock-free, so the writer
     * never waits on the reader for longer than a swap.
     */
    [[nodiscard]] std::vector<T> drain() {
        std::vector<T> taken;
        const std::scoped_lock lock(mutex_);
        taken.swap(buffer_);
        return taken;
    }

    /** @brief How many values are waiting. */
    [[nodiscard]] std::size_t size() const {
        const std::scoped_lock lock(mutex_);
        return buffer_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<T> buffer_;
};

/** @brief The interface to runtime direction. */
using command_queue = channel<command>;

/** @brief The runtime to interface direction. */
using event_queue = channel<event::event>;

}

#endif
