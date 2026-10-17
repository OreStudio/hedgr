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
#ifndef HEDGR_MODEL_DOMAIN_DEPLOYMENT_HPP
#define HEDGR_MODEL_DOMAIN_DEPLOYMENT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace hedgr::model::domain {

/**
 * @brief One tenant as the command centre lists it.
 *
 * The fields are the deployment's own words, copied rather than translated. A
 * status is a code the deployment defines, and a client that mapped it to a
 * sentence of its own would show a state the deployment had renamed.
 */
struct tenant_row final {
    /** @brief Stable key, used as the row's identity and never shown. */
    std::string id;

    /** @brief The tenant's display name. */
    std::string name;

    /** @brief The deployment's lifecycle status, for example "active". */
    std::string status;

    /** @brief The deployment's tenant type, for example "evaluation". */
    std::string type;
};

/**
 * @brief One deployment, read in one go.
 *
 * The counts and the roster arrive together on purpose. A dashboard that drew
 * four counters from four independent reads would eventually disagree with
 * itself, and a reader would have no way to tell which number was stale. One
 * snapshot is one moment, so everything on the screen is about that moment.
 *
 * The read is all or nothing: a failed call leaves @c failure set and the
 * counts at zero, and the screen says the read failed rather than drawing
 * zeros, because a zero count is a fact and an unread deployment is not.
 */
struct deployment_snapshot final {
    /**
     * @brief When the read finished, on the monotonic clock.
     *
     * Zero means no read has ever succeeded, which is the state the Overview
     * draws as "not read yet" rather than as an empty estate.
     */
    std::int64_t observed_at_ns = 0;

    /** @brief Every tenant the system administrator may see, system aside. */
    std::uint64_t tenant_total = 0;

    /** @brief Tenants whose lifecycle status is active. */
    std::uint64_t in_service = 0;

    /** @brief Tenants whose type is evaluation. */
    std::uint64_t on_evaluation = 0;

    /** @brief Tenants whose lifecycle status is bootstrapping. */
    std::uint64_t setting_up = 0;

    /** @brief Tenants whose lifecycle status is suspended. */
    std::uint64_t needing_attention = 0;

    /** @brief The first page of the roster, in the deployment's own order. */
    std::vector<tenant_row> tenants;

    /**
     * @brief The read that failed, or empty when the snapshot is whole.
     *
     * The subject rather than a sentence, because a person reading a message
     * needs to know which read broke and an engineer needs the name of the
     * call. The sentence is built where the message is drawn.
     */
    std::string failing_read;

    /** @brief The transport's own words for the failure, or empty. */
    std::string failure;

    /** @brief Whether this snapshot holds a completed read. */
    [[nodiscard]] bool is_read() const noexcept {
        return observed_at_ns != 0 && failure.empty();
    }
};

}

#endif
