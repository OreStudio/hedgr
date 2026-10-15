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
#include "hedgr.model/state/probe_table.hpp"
#include <memory>
#include <utility>

namespace hedgr::model::state {

probe_table::row& probe_table::resolve(instance_handle h) {
    if (rows_.size() <= h.index)
        rows_.resize(static_cast<std::size_t>(h.index) + 1);

    auto& slot = rows_[h.index];

    /*
     * A generation that differs from the incoming handle describes a different
     * instance, so the row it held is dropped rather than inherited: a new
     * occupant must not begin life looking as though it had already been
     * probed, nor waiting on a deadline somebody else's failures produced.
     */
    if (slot.generation != h.generation)
        slot = row{};

    slot.generation = h.generation;
    return slot;
}

void probe_table::set(instance_handle h, domain::probe_result result) {
    if (!h.is_valid())
        return;
    resolve(h).result = std::move(result);
}

const probe_table::row* probe_table::find(instance_handle h) const noexcept {
    if (!h.is_valid() || h.index >= rows_.size())
        return nullptr;

    const auto& slot = rows_[h.index];
    if (slot.generation == 0 || slot.generation != h.generation)
        return nullptr;
    return std::addressof(slot);
}

const domain::probe_result*
probe_table::get(instance_handle h) const noexcept {
    const auto* slot = find(h);
    if (slot == nullptr || !slot->result)
        return nullptr;
    return std::addressof(*slot->result);
}

void probe_table::record_snapshot(std::int64_t observed_at_ns,
    std::size_t total) {
    estate_sample sample;
    sample.observed_at_ns = observed_at_ns;

    for (const auto& slot : rows_) {
        if (slot.generation == 0 || !slot.result)
            continue;

        /*
         * The three states are read in the order the tree's own colour helper
         * reads them, because an unreachable broker has said nothing about IAM
         * and counting it as merely not ready would turn an outage into a
         * deployment that is halfway through setting itself up.
         */
        if (slot.result->reach == domain::reachability::unreachable)
            ++sample.unreachable;
        else if (slot.result->ready == domain::readiness::ready)
            ++sample.ready;
        else
            ++sample.not_ready;
    }

    /*
     * The remainder is every live instance with no answer yet, which includes
     * one whose first probe is still in flight. It is the fourth colour the
     * tree draws rather than a gap, so the four counts always sum to the
     * estate.
     */
    const auto answered = static_cast<std::size_t>(
        sample.ready + sample.not_ready + sample.unreachable);
    sample.unknown = static_cast<std::int32_t>(
        total > answered ? total - answered : 0);

    estate_.append(sample);
}

const estate_history& probe_table::estate() const noexcept {
    return estate_;
}

void probe_table::forget(instance_handle h) {
    if (find(h) == nullptr)
        return;
    rows_[h.index] = row{};
}

std::size_t probe_table::size() const noexcept {
    std::size_t count = 0;
    for (const auto& slot : rows_) {
        if (slot.generation != 0)
            ++count;
    }
    return count;
}

void probe_table::clear() noexcept {
    rows_.clear();
    estate_.clear();
}

void probe_table::set_next_probe_ns(instance_handle h, std::int64_t value) {
    if (!h.is_valid())
        return;
    resolve(h).next_probe_ns = value;
}

std::int64_t probe_table::next_probe_ns(instance_handle h) const noexcept {
    const auto* slot = find(h);
    return slot == nullptr ? 0 : slot->next_probe_ns;
}

}
