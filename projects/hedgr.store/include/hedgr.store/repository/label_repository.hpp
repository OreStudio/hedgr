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
#ifndef HEDGR_STORE_REPOSITORY_LABEL_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_LABEL_REPOSITORY_HPP

#include "hedgr.model/domain/label.hpp"
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
 * @brief Reads and writes the labels an operator has declared.
 *
 * The repository borrows the connection, so it must not outlive it. Both the
 * @c colour and the @c severity columns are stored as their canonical text so
 * the database is readable, and either failing to parse is reported as a
 * corrupt row rather than silently becoming the default.
 */
class HEDGR_STORE_EXPORT label_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit label_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c label table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Every label, ordered by name. */
    [[nodiscard]] result<std::vector<dom::label>> list() const;

    /** @brief Inserts @c row, or replaces the row with the same id. */
    [[nodiscard]] result<void> save(const dom::label& row);

    /** @brief Deletes the label with @c id, cascading to its attachments. */
    [[nodiscard]] result<void> remove(std::string_view id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
