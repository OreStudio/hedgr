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
#ifndef HEDGR_MODEL_DOMAIN_SHORTCUT_HPP
#define HEDGR_MODEL_DOMAIN_SHORTCUT_HPP

#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>

namespace hedgr::model::domain {

/**
 * @brief One entry on the shortcuts bar.
 *
 * The target is a window kind beside an id, and the kind is the same
 * discriminator a window carries, because a shortcut opens a surface and the
 * surfaces are exactly the window kinds. Modelling it as an instance reference
 * would have made the Instances window itself, which is the first shortcut an
 * operator wants, inexpressible.
 *
 * @c target_id names the row the target concerns and is empty when the kind
 * concerns none, so the Instances window is the instance_manager kind with no
 * id and an accounts shortcut is the accounts kind with an instance id.
 *
 * A target that is removed takes its shortcuts with it, removed by the same
 * command that removes the row, so the bar never offers an entry that opens
 * something that is gone. A shortcut that named a target the store no longer
 * holds is therefore not a state this type has to describe. The removal step is
 * what stops it arising, and a store write that refuses the removal is reported
 * rather than left as a silent dangling entry.
 */
struct shortcut final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief The kind of surface the shortcut opens. */
    window_kind target_kind = window_kind::instance_manager;

    /**
     * @brief The row the target concerns.
     *
     * Empty for a kind that concerns no row, such as the Instances window. For
     * a kind that does, it is the instance or account key the surface is about,
     * and it is what a target removal matches on.
     */
    std::string target_id;

    /**
     * @brief Where the entry sits on the bar, ascending.
     *
     * A plain number rather than an index into a container, because the order
     * belongs to the operator and survives a row being added beside it. Two
     * entries may share it, and the id breaks the tie, so the order is total.
     */
    std::int64_t position = 0;

    friend bool operator==(const shortcut&, const shortcut&) = default;
};

}

#endif
