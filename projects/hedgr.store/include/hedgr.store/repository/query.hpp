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
#ifndef HEDGR_STORE_REPOSITORY_QUERY_HPP
#define HEDGR_STORE_REPOSITORY_QUERY_HPP

#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <initializer_list>
#include <sqlgen/dynamic/Insert.hpp>
#include <sqlgen/insert.hpp>
#include <sqlgen/transpilation/to_insert_or_write.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::store::repository {

/**
 * @brief The adapters that turn sqlgen's answers into this component's.
 *
 * sqlgen reports failure as @c rfl::Result and a sentence. Every repository
 * ends there, at the boundary, with one of these three: the failure becomes a
 * @c store_error, a read's rows come back as the container the caller asked
 * for, and a write becomes a plain success. Nothing above this component sees
 * an @c rfl type.
 */

/** @brief A sqlgen failure as a store failure, classified where it can be. */
[[nodiscard]] HEDGR_STORE_EXPORT sqlite::store_error
query_failed(const rfl::Error& error);

/**
 * @brief The current UTC instant, in the format the store's columns hold.
 *
 * SQLite's own @c datetime('now') used to fill these columns from a DEFAULT
 * clause. sqlgen cannot express a column default, so the value is produced
 * here instead, and in the same format that clause wrote: @c "YYYY-MM-DD
 * HH:MM:SS", in UTC.
 */
[[nodiscard]] HEDGR_STORE_EXPORT std::string timestamp_now();

/** @brief The rows of a read, or the failure it produced. */
template <class Rows>
[[nodiscard]] sqlite::result<Rows> rows_of(const rfl::Result<Rows>& rows) {
    if (!rows)
        return std::unexpected(query_failed(rows.error()));
    return rows.value();
}

/** @brief The outcome of a write, or the failure it produced. */
template <class Outcome>
[[nodiscard]] sqlite::result<void> done(const rfl::Result<Outcome>& outcome) {
    if (!outcome)
        return std::unexpected(query_failed(outcome.error()));
    return {};
}

/**
 * @brief Inserts @p row, or replaces the row @p key_columns identify.
 *
 * The columns come from @p Entity, in the order it declares them, and only the
 * conflict target is chosen here. sqlgen's own @c insert_or_replace derives
 * that target from every primary and unique column the entity declares, which
 * is one column too many for a table that is keyed by its id and happens to
 * have a unique name as well: SQLite refuses a conflict target that no
 * constraint matches, and there is no unique index over the pair. Naming the
 * key is therefore the caller's business, and every caller here names the
 * columns that actually key its table -- which for the two label junctions is
 * the pair that is their primary key.
 *
 * A save that replaces a row leaves every column the entity does not mention
 * alone, which is how a credential survives an edit of the account that holds
 * it.
 */
template <class Entity>
[[nodiscard]] sqlite::result<void> upsert(sqlite::database& db,
    const Entity& row, std::initializer_list<std::string_view> key_columns) {
    auto statement =
        sqlgen::transpilation::to_insert_or_write<Entity,
            sqlgen::dynamic::Insert>(false);
    statement.or_replace = true;
    statement.constraints.clear();
    for (const auto column : key_columns)
        statement.constraints.emplace_back(column);

    std::vector<Entity> rows;
    rows.push_back(row);
    return done(db.connection()->insert(statement, rows.begin(), rows.end()));
}

}

#endif
