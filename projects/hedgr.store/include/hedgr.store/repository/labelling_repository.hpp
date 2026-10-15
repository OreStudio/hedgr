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
#ifndef HEDGR_STORE_REPOSITORY_LABELLING_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_LABELLING_REPOSITORY_HPP

#include "hedgr.model/domain/labelling.hpp"
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
 * @brief Reads and writes the attachments of labels to owners.
 *
 * The junction lives in two tables and the repository presents it as one list,
 * because the owner kind is what a caller thinks in and the two tables are only
 * an artifact of each foreign key naming the table it cascades on. Nothing here
 * parses: a labelling is two stored ids and a kind the runtime supplies.
 */
class HEDGR_STORE_EXPORT labelling_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit labelling_repository(sqlite::database& db) noexcept;

    /** @brief Every attachment of both kinds. */
    [[nodiscard]] result<std::vector<dom::labelling>> list() const;

    /**
     * @brief Attaches @c row.
     *
     * The pair is the primary key on both tables, so re-attaching the same pair
     * is a no-op rather than a failure.
     */
    [[nodiscard]] result<void> save(const dom::labelling& row);

    /** @brief Detaches @c row. */
    [[nodiscard]] result<void> remove(const dom::labelling& row);

    /** @brief Detaches every label from one owner. */
    [[nodiscard]] result<void>
    remove_owner(dom::labelling_owner owner, std::string_view owner_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
