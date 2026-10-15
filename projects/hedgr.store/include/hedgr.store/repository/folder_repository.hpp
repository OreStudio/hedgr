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
#ifndef HEDGR_STORE_REPOSITORY_FOLDER_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_FOLDER_REPOSITORY_HPP

#include "hedgr.model/domain/folder.hpp"
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
 * @brief Reads and writes the folders of every tree.
 *
 * The repository borrows the connection, so it must not outlive it. Only the
 * @c tree column needs parsing: it is stored as its canonical text so a person
 * reading the database sees what the application means, and a value that is not
 * one of the known trees is reported as a corrupt row rather than defaulted.
 *
 * The @c folder table is described by @c folder_entity, and so is the partial
 * unique index that makes the single root a database fact.
 */
class HEDGR_STORE_EXPORT folder_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit folder_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c folder table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Every folder, ordered by tree then name. */
    [[nodiscard]] result<std::vector<dom::folder>> list() const;

    /**
     * @brief Inserts @c row, or replaces the row with the same id.
     *
     * A second root in one tree is refused by the partial unique index rather
     * than by this method, so the constraint holds for every writer.
     */
    [[nodiscard]] result<void> save(const dom::folder& row);

    /** @brief Deletes the folder with @c id. */
    [[nodiscard]] result<void> remove(std::string_view id);

    /** @brief Deletes every folder of one tree owned by one owner. */
    [[nodiscard]] result<void>
    remove_tree(dom::folder_tree tree, std::string_view owner_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
