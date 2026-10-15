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
#ifndef HEDGR_MODEL_DOMAIN_STORE_LOCK_HPP
#define HEDGR_MODEL_DOMAIN_STORE_LOCK_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string_view>

namespace hedgr::model::domain {

/**
 * @brief Whether the master password is held, and whether one exists.
 *
 * The whole of the local store sits behind the master password, not only the
 * credentials: the instance catalogue, the folders and the labels are as
 * unreadable as the secrets while the lock is closed. Losing the master
 * password therefore loses the configuration as well as the credentials, and
 * there is no recovery for either. That is the blast radius, and it is a
 * deliberate choice rather than a defect to be fixed later.
 *
 * @c locked is a state the interface draws and not an error it reports. A
 * closed lock is the normal condition of a store at rest, so nothing here is
 * ever worded as a failure: only @c failed says an attempt was refused.
 */
enum class store_lock : std::uint8_t {
    /**
     * @brief No master password has been set yet.
     *
     * The first launch on a new store. The interface has to offer to create
     * one before it can read the catalogue, because every read is a decryption.
     */
    absent = 0,
    /** @brief A master password exists and is not held. */
    locked = 1,
    /** @brief The master password is held for this run. */
    unlocked = 2,
    /** @brief The last attempt to unlock was refused. */
    failed = 3,
};

/** @brief The number of lock states, for indexing a lookup table. */
inline constexpr std::size_t store_lock_count = 4;

/** @brief The canonical text of a lock state, for logs and tooltips. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(store_lock value) noexcept;

}

#endif
