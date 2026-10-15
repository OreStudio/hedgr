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
#ifndef HEDGR_STORE_REPOSITORY_INSTANCE_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_INSTANCE_REPOSITORY_HPP

#include "hedgr.model/domain/instance.hpp"
#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

// The repositories speak stored text on one side and domain values on the
// other, so the two namespaces they name are aliased once here.
namespace dom = model::domain;
using sqlite::result;

/**
 * @brief Reads and writes the instances an operator has configured.
 *
 * The repository borrows the connection, so it must not outlive it. It is a
 * thin translation between the @c instance table and the domain value: every
 * column here is stored text or a plain number, so nothing needs parsing.
 */
class HEDGR_STORE_EXPORT instance_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit instance_repository(sqlite::database& db) noexcept;

    /** @brief Every configured instance, ordered by name. */
    [[nodiscard]] result<std::vector<dom::instance>> list() const;

    /**
     * @brief Inserts @c row, or replaces the row with the same id.
     *
     * Rows are keyed by id, so saving the same instance twice updates it rather
     * than duplicating it.
     */
    [[nodiscard]] result<void> save(const dom::instance& row);

    /** @brief Deletes the instance with @c id, cascading to its accounts. */
    [[nodiscard]] result<void> remove(std::string_view id);

    /**
     * @brief Moves the instance into @p folder_id, leaving every other column.
     *
     * A call of its own rather than a read-modify-write through @c save,
     * because a move happens when a folder is removed and its contents are
     * re-homed: that path holds a set of keys and a destination and nothing
     * else, and reconstructing the whole row to move it would be a way to write
     * back a stale copy of the rest of it.
     */
    [[nodiscard]] result<void>
    set_parent(std::string_view id, std::string_view folder_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
