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
#include "hedgr.store/repository/schema.hpp"
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <sqlgen/read.hpp>
#include <sqlgen/order_by.hpp>
#include <sqlgen/literals.hpp>
#include <string>
#include <vector>

namespace hedgr::store::repository {

namespace {

/**
 * @brief The schema version written and expected by this component.
 *
 * Bumped from 1 when an instance row stopped carrying a safety classification
 * and the accounts table took its current name, from 2 when the folders, labels
 * and label attachments were added and an instance stopped carrying a
 * comma-separated label string, from 3 when an account's free-text `label`
 * column became `name` so that it no longer shares a word with the label
 * entity, from 4 when the store gained the master table and the account gained
 * its sealed credential, from 5 when an instance and an account gained the
 * folder they sit in, from 6 when the tables stopped being hand-written
 * SQL and started being derived from the entity structs, which moved the
 * `created_at` default out of the table and into the writer, and from 7 when an
 * instance gained its own TLS material as three paths. The store is
 * pre-release, so there is no migration: a file written against a different
 * version is refused, and the operator deletes it.
 */
constexpr std::int64_t current_version = 7;

/**
 * @brief The version the file was written with, when it records one.
 *
 * The newest row wins rather than the only one: the table should hold a single
 * row, but a file that somehow accreted more must not be read as whatever
 * SQLite happens to return first.
 */
result<std::optional<std::int64_t>> stored_version(sqlite::database& db) {
    using namespace sqlgen::literals;
    const auto query =
        sqlgen::read<std::vector<schema_version_entity>> |
        sqlgen::order_by("version"_c.desc());

    auto rows = query(db.connection());
    if (!rows) {
        return std::unexpected(
            sqlite::from_sqlgen(rows.error(), sqlite::store_errc::step_failed));
    }
    if (rows.value().empty())
        return std::optional<std::int64_t>{};
    return std::optional<std::int64_t>{rows.value().front().version};
}

/**
 * @brief The failure that tells the operator to delete the file.
 *
 * The path, both versions and the remedy are all in the sentence, because this
 * is the one store failure a person has to act on by hand and it reaches them
 * through a window with room for a line or two. It is deliberately not a
 * "retry" or an "internal error": nothing in the application can fix it.
 */
sqlite::store_error version_mismatch(const std::filesystem::path& path,
    std::int64_t stored) {
    return sqlite::store_error{
        .code = sqlite::store_errc::schema_mismatch,
        .message = std::format(
            "the store at {} was written with schema version {} but this build "
            "expects version {}; this store is pre-release and is not migrated, "
            "so delete the file and start again",
            path.string(), stored, current_version),
        .sqlite_code = 0,
    };
}

}

std::int64_t schema_version() noexcept {
    return current_version;
}

result<void> apply(sqlite::database& db) {
    if (!db.is_open())
        return std::unexpected(sqlite::store_error{
            .code = sqlite::store_errc::closed,
            .message = "cannot apply the schema to a closed database",
            .sqlite_code = 0,
        });

    /*
     * The tables are created from the entity structs, so a column renamed in
     * one place cannot survive in the other: the statement that creates it and
     * the code that binds it are two readings of one declaration. The three
     * tables that carry a cascade foreign key, and the two composite keys that
     * come with them, are the exceptions and are listed with their reason in
     * ddl.cpp.
     */
    for (const auto& table : tables()) {
        if (auto applied = db.execute(table.text); !applied)
            return std::unexpected(applied.error());
    }

    for (const auto& statement : indexes()) {
        if (auto applied = db.execute(statement); !applied)
            return std::unexpected(applied.error());
    }

    /*
     * Every statement above is IF NOT EXISTS, so an existing file keeps the
     * columns it was created with and nothing here notices. Reading the version
     * back is what notices, and it is the reason a stale file is diagnosed as a
     * stale file rather than as a query against a column that no longer exists:
     * without this, an old database opened fine and then failed somewhere far
     * from the cause, once per query, with a message about whichever column the
     * current build happens to name first.
     */
    auto stored = stored_version(db);
    if (!stored)
        return std::unexpected(stored.error());

    if (stored.value() && stored.value().value() != current_version)
        return std::unexpected(version_mismatch(db.path(), stored.value().value()));

    /*
     * No stored version means a file with no history. Since every version this
     * store has ever written wrote a row, the only way to reach this is a file
     * created empty by this call, so stamping the current version is right. A
     * file from before schema_version existed cannot be told apart from an
     * empty one, which is the pre-release case the entry above refuses for
     * every version that can.
     */
    if (stored.value())
        return {};

    return db.execute(
        std::format("INSERT INTO schema_version (version) VALUES ({})",
                    current_version));
}

}
