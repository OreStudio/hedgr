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

namespace hedgr::store::repository {

// @c database and @c result are both spelled in the sqlite subfacet, and this
// header mentions each of them once, so the spelling is shortened here.
using sqlite::database;
using sqlite::result;

/**
 * @brief Creates the store's tables and records the schema version.
 *
 * Idempotent: every statement is @c IF @c NOT @c EXISTS, and the version row is
 * written only when the version table is empty, so applying the schema to a
 * fresh file and to an up-to-date one both succeed and leave the same shape.
 *
 * No table has a credential column and none may gain one. The store this
 * replaces, OreStudio's @c ores.connections, kept an AES-256-GCM encrypted
 * password alongside a master password. Hedgr deliberately does not: a
 * credential exists only for the length of one connect attempt, so persisting
 * it would put a secret at rest that the application never reads back. See the
 * @c Credential entry in CONTEXT.md.
 */
[[nodiscard]] HEDGR_STORE_EXPORT result<void> apply(database& db);

}

#endif
