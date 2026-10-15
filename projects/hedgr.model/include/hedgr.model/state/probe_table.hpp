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
#ifndef HEDGR_MODEL_STATE_PROBE_TABLE_HPP
#define HEDGR_MODEL_STATE_PROBE_TABLE_HPP

#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/handle.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief The last probe result and the next probe deadline, per instance.
 *
 * A table of its own rather than columns on the instance row, which is the
 * whole point: an instance is configuration and a probe result is a fact about
 * a moment, so folding the second into the first would make a cold
 * configuration value something written on every poll and, worse, a fact that
 * was true once would be persisted into a file that describes what an operator
 * configured.
 *
 * The rows run parallel to the instance table because they are keyed the same
 * way: by the generation-tagged handle. A row therefore carries the instance
 * generation it was written for, and a handle whose generation no longer
 * matches addresses nothing. That is what stops a probe result for a removed
 * instance from being read back as the result for whichever instance takes the
 * slot next.
 *
 * A row is also where the polling schedule lives: every row holds the next
 * deadline for its instance, so "when is this due" is answered from the same
 * place as "what did it last say". A row may hold a deadline and no result,
 * which is the state an instance is in before its first probe has answered.
 */
class HEDGR_MODEL_EXPORT probe_table final {
public:
    /**
     * @brief Records @p result as what the probe of @p h found.
     *
     * Creates the row when there is none, and reuses it when the generation
     * still matches, so a result never disturbs the deadline beside it. A
     * handle for a slot that has since been reissued starts a fresh row rather
     * than inheriting the previous occupant's schedule.
     */
    void set(instance_handle h, domain::probe_result result);

    /**
     * @brief The result for @p h, or nullptr when there is none.
     *
     * Nullptr covers both of the ways there can be nothing to report: a handle
     * whose slot now holds a different instance, and a live instance that no
     * probe has answered for yet. A caller draws them the same way, because
     * both mean nobody knows.
     */
    [[nodiscard]] const domain::probe_result*
    get(instance_handle h) const noexcept;

    /**
     * @brief Drops the row for @p h, result and deadline together.
     *
     * Called when an instance goes, so a probe result cannot outlive the
     * instance it describes.
     */
    void forget(instance_handle h);

    /** @brief How many instances this table holds a row for. */
    [[nodiscard]] std::size_t size() const noexcept;

    void clear() noexcept;

    /**
     * @brief Sets when @p h is next due to be probed, in nanoseconds.
     *
     * Creates the row when there is none, because scheduling an instance that
     * has never answered is exactly how its first probe happens.
     */
    void set_next_probe_ns(instance_handle h, std::int64_t value);

    /**
     * @brief When @p h is next due, or zero when it is not known.
     *
     * Zero reads as "due now" rather than as "never": an instance with no row
     * is one the schedule has not been told about, and the useful thing to do
     * with such an instance is to probe it rather than skip it forever.
     */
    [[nodiscard]] std::int64_t next_probe_ns(instance_handle h) const noexcept;

private:
    struct row final {
        /**
         * @brief The instance generation this row was written for.
         *
         * Zero means the row is free. A slab issues generations from one
         * upwards, so no real handle carries zero and the free slot cannot be
         * mistaken for a live one.
         */
        std::uint32_t generation = 0;
        std::int64_t next_probe_ns = 0;
        std::optional<domain::probe_result> result;
    };

    /** @brief The row for @p h, or nullptr when the handle is stale. */
    [[nodiscard]] const row* find(instance_handle h) const noexcept;

    /** @brief The row for @p h, creating it when the handle is live. */
    [[nodiscard]] row& resolve(instance_handle h);

    std::vector<row> rows_;
};

}

#endif
