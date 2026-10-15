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
#include "hedgr.model/state/estate_history.hpp"

namespace hedgr::model::state {

void estate_history::append(const estate_sample& sample) noexcept {
    samples_[next_] = sample;
    samples_[next_ + capacity] = sample;
    next_ = next_ + 1 == capacity ? 0 : next_ + 1;
    if (count_ < capacity)
        ++count_;
}

const estate_sample* estate_history::data() const noexcept {
    return samples_.data() + start();
}

const estate_sample& estate_history::at(std::size_t index) const noexcept {
    return data()[index];
}

std::size_t estate_history::size() const noexcept {
    return count_;
}

bool estate_history::empty() const noexcept {
    return count_ == 0;
}

void estate_history::clear() noexcept {
    next_ = 0;
    count_ = 0;
}

std::size_t estate_history::start() const noexcept {
    /*
     * While the ring still has room the samples were written from slot zero and
     * the oldest is still there. Once it is full the next append overwrites the
     * oldest, so that slot becomes where the held run begins.
     */
    return count_ < capacity ? 0 : next_;
}

}
