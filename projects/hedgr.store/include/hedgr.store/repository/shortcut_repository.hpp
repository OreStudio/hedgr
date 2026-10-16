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
#ifndef HEDGR_STORE_REPOSITORY_SHORTCUT_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_SHORTCUT_REPOSITORY_HPP

#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

// The repositories speak stored text on one side and domain values on the
// other, so the two namespaces they name are aliased once here.
namespace dom = model::domain;
using sqlite::result;

/**
 * @brief Reads and writes the entries of the shortcuts bar.
 *
 * The repository borrows the connection, so it must not outlive it. The target
 * kind is stored as the window kind's canonical text and parsed on the way
 * back, so an entry that names a kind this build does not know is reported as a
 * corrupt row rather than silently becoming another kind.
 *
 * The bar is read as one ordered list, because the order is the whole of what
 * the bar is. Every caller wants the entries in the sequence the operator put
 * them in, and none of them wants one entry beside its position.
 */
class HEDGR_STORE_EXPORT shortcut_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit shortcut_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c shortcut table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Every entry, ordered by position and then by id. */
    [[nodiscard]] result<std::vector<dom::shortcut>> list() const;

    /** @brief Inserts @c row, or replaces the entry with the same id. */
    [[nodiscard]] result<void> save(const dom::shortcut& row);

    /** @brief Deletes the entry with @c id. */
    [[nodiscard]] result<void> remove(std::string_view id);

    /**
     * @brief Deletes every entry that opens @p target_id as @p kind.
     *
     * The call a target removal makes, because no foreign key can express this
     * one. The target is a kind and an id, and the rows it names live in tables
     * the shortcut table does not reference. Saying it here as one statement
     * keeps the whole cascade in the store rather than in a walk over what was
     * read earlier.
     */
    [[nodiscard]] result<void>
    remove_for_target(dom::window_kind kind, std::string_view target_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
