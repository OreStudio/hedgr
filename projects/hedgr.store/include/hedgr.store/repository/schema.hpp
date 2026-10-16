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
#ifndef HEDGR_STORE_REPOSITORY_SCHEMA_HPP
#define HEDGR_STORE_REPOSITORY_SCHEMA_HPP

#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <cstdint>

namespace hedgr::store::repository {

// @c database and @c result are both spelled in the sqlite subfacet, and this
// header mentions each of them once, so the spelling is shortened here.
using sqlite::database;
using sqlite::result;

/**
 * @brief Creates the store's tables, then checks the file's schema version.
 *
 * Idempotent for a file this build wrote: every statement is @c IF @c NOT
 * @c EXISTS, and the version row is written only when the version table is
 * empty, so applying the schema to a fresh file and to an up-to-date one both
 * succeed and leave the same shape. Migration is idempotent for the same
 * reason. It runs only while the file still says version 7 and it stamps 8 as
 * part of the same transaction, so applying the schema again leaves it alone.
 *
 * A file written by version 7 is migrated forward. The missing @c icon_key
 * column is added as empty on every row and the @c shortcut table is created,
 * both inside one transaction so a failure leaves the file as it was. A file
 * written against any other version is refused with
 * @c store_errc::schema_mismatch. The @c IF @c NOT @c EXISTS statements would
 * otherwise leave an old table with its old columns and fail later, once per
 * query, far from the cause, so the version is read back and compared. The file
 * is not deleted: it holds an operator's instances, and destroying them to save
 * a rebuild would be worse than refusing to start. See the sentence in the
 * returned error for the remedy.
 *
 * One table holds a secret and one column on another does, and both hold
 * ciphertext. The @c master table keeps a known sentence sealed under the
 * master password, which is how the password is checked without ever being
 * stored; @c account.credential keeps a credential sealed the same way. Nothing
 * here can be read without the master password, and nothing here contains it.
 * See ADR 4 and the @c Master @c password entry in CONTEXT.md.
 */
[[nodiscard]] HEDGR_STORE_EXPORT result<void> apply(database& db);

/**
 * @brief The schema version this build writes and expects.
 *
 * Read by tests rather than restated in them: a version written into a test as
 * a literal is a second place it has to be bumped, and the one that forgets is
 * the one that makes a passing test lie.
 */
[[nodiscard]] HEDGR_STORE_EXPORT std::int64_t schema_version() noexcept;

}

#endif
