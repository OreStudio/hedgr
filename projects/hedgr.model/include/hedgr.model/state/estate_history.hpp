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
#ifndef HEDGR_MODEL_STATE_ESTATE_HISTORY_HPP
#define HEDGR_MODEL_STATE_ESTATE_HISTORY_HPP

#include "hedgr.model/export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace hedgr::model::state {

/**
 * @brief How many instances stood in each state at one poll.
 *
 * The counts are the colours the instance tree and its chart draw rather than a
 * fold into one total, because the chart and the rows must not become two
 * opinions about the same estate. An instance nothing is known about is counted
 * as unknown rather than left out, so the counts sum to every live instance and
 * a chart of them can reach one hundred percent.
 */
struct estate_sample final {
    /** @brief When the poll these counts describe was taken, monotonic. */
    std::int64_t observed_at_ns = 0;

    /** @brief Instances whose probe answered that the deployment is set up. */
    std::int32_t ready = 0;

    /** @brief Instances whose probe answered that setup is still incomplete. */
    std::int32_t not_ready = 0;

    /**
     * @brief Instances whose broker answered but IAM did not.
     *
     * A fault rather than a question. An instance that still needs bootstrapping
     * answers the setup read, so a read nothing answered is a configured
     * deployment whose service is not there, and it is counted apart from the
     * broker that never answered because the two have different fixes. See
     * @c theme::probe_state_colour for the same rule.
     */
    std::int32_t service_missing = 0;

    /** @brief Instances no connection could be opened to. */
    std::int32_t unreachable = 0;

    /** @brief Instances no probe has answered for yet. */
    std::int32_t unknown = 0;

    /**
     * @brief Every live instance these counts describe.
     *
     * Inline rather than out of line because it is the sum of five numbers and
     * the struct is not itself an exported type, so a caller in another library
     * would otherwise have nothing to link against.
     */
    [[nodiscard]] std::int32_t total() const noexcept {
        return ready + not_ready + service_missing + unreachable + unknown;
    }
};

/**
 * @brief The recent polls of the whole estate, oldest first.
 *
 * A ring of a fixed capacity rather than a growing list, because the chart is a
 * window on the recent past and an unbounded list would grow for the life of
 * the process and move the axis every time it filled.
 *
 * Every sample is written twice, once at its slot and once a capacity further
 * on. That is what lets a reader take the samples as one contiguous run in time
 * order whichever way the ring has wrapped, so the plot walks one stride-1
 * array with no per-sample allocation and no work to join two halves.
 *
 * The capacity is chosen from the runtime's probe cadence. At one sample every
 * few seconds the slots here cover half an hour, which is long enough to watch a
 * deployment flap and short enough that the newest poll is still what the
 * operator came to look at.
 */
class HEDGR_MODEL_EXPORT estate_history final {
public:
    /** @brief How many polls the estate's history holds. */
    static constexpr std::size_t capacity = 360;

    /**
     * @brief Records @p sample as the newest, dropping the oldest at capacity.
     *
     * Constant time and allocation free, so the writer and the chart's reader
     * do no work per sample beyond the two stores below.
     */
    void append(const estate_sample& sample) noexcept;

    /** @brief The samples, oldest first, as one contiguous run. */
    [[nodiscard]] const estate_sample* data() const noexcept;

    /** @brief The sample @p index places from the oldest. */
    [[nodiscard]] const estate_sample& at(std::size_t index) const noexcept;

    /**
     * @brief How many polls are held, which is the capacity once it is full.
     *
     * A reader plots exactly this many samples, so an estate polled fewer times
     * than the capacity draws only what it has and never a zero standing in for
     * a poll nobody took.
     */
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept;

    void clear() noexcept;

private:
    /** @brief Where the oldest held sample starts in the doubled array. */
    [[nodiscard]] std::size_t start() const noexcept;

    /*
     * Each sample sits at its own slot and again one capacity later, so the run
     * from the oldest sample to the newest never crosses a seam and a reader
     * never has to be handed two halves.
     */
    std::array<estate_sample, capacity * 2> samples_{};

    /** @brief The slot the next append writes, in 0 up to capacity minus one. */
    std::size_t next_ = 0;

    /** @brief How many samples have been appended, capped at the capacity. */
    std::size_t count_ = 0;
};

}

#endif
