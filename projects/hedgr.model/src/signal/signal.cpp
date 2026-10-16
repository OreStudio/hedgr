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
#include "hedgr.model/signal/signal.hpp"
#include <utility>

namespace hedgr::model::signal {

void signal_bus::raise(signal value) {
    /*
     * The lock is held only for the push. A raiser never waits on a listener,
     * which is what makes raising from a worker safe and raising from the frame
     * loop free.
     */
    std::lock_guard lock(mutex_);
    raised_.push_back(std::move(value));
}

std::vector<signal> signal_bus::drain() {
    std::vector<signal> drained;
    /*
     * A swap rather than a copy and clear, so the drain takes the whole inbox
     * in one move and a signal raised in the meantime lands in the empty vector
     * that is left behind rather than being lost or taken twice.
     */
    std::lock_guard lock(mutex_);
    drained.swap(raised_);
    return drained;
}

bool signal_bus::empty() const {
    std::lock_guard lock(mutex_);
    return raised_.empty();
}

signal_bus& bus() {
    /*
     * A function-local static, so the bus is built on first use and destroyed
     * with the process. The order is safe because nothing raises during static
     * destruction.
     */
    static signal_bus instance;
    return instance;
}

}
