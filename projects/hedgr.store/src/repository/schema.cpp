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
#include "hedgr.store/sqlite/statement.hpp"
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace hedgr::store::repository {

namespace {

/** @brief The schema version written and expected by this component. */
constexpr std::int64_t current_version = 1;

/**
 * @brief The whole schema, in one place.
 *
 * The tables mirror the domain types rather than any view's needs: an instance
 * carries its tier and transport, an identity carries no credential, and a
 * window carries its kind beside its payload so no variant is ever written out.
 *
 * @c window_spec.session_instance_id and @c window_spec.session_identity_id are
 * nullable because a window may be bound to no session at all, which is the
 * normal case for the instance manager.
 */
constexpr std::array<std::string_view, 5> schema_statements{
    R"(PRAGMA foreign_keys = ON)",
    R"(CREATE TABLE IF NOT EXISTS instance (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        tier TEXT NOT NULL,
        nats_url TEXT NOT NULL,
        subject_prefix TEXT NOT NULL,
        wire_format TEXT NOT NULL DEFAULT 'msgpack',
        labels TEXT NOT NULL DEFAULT '',
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    ))",
    R"(CREATE TABLE IF NOT EXISTS identity (
        id TEXT PRIMARY KEY,
        instance_id TEXT NOT NULL REFERENCES instance(id) ON DELETE CASCADE,
        label TEXT NOT NULL,
        principal TEXT NOT NULL,
        UNIQUE(instance_id, principal)
    ))",
    R"(CREATE TABLE IF NOT EXISTS window_spec (
        window_id TEXT PRIMARY KEY,
        kind TEXT NOT NULL,
        session_instance_id TEXT,
        session_identity_id TEXT,
        cadence_ms INTEGER NOT NULL DEFAULT 1000,
        state_json TEXT NOT NULL DEFAULT '{}'
    ))",
    R"(CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER NOT NULL
    ))",
};

/** @brief Whether the schema version table already holds a row. */
result<bool> has_version(sqlite::database& db) {
    sqlite::statement query(db.handle(),
                            "SELECT COUNT(*) FROM schema_version");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    auto row = query.step();
    if (!row)
        return std::unexpected(row.error());
    return query.column_int64(0) > 0;
}

}

result<void> apply(sqlite::database& db) {
    if (!db.is_open())
        return std::unexpected(sqlite::store_error{
            .code = sqlite::store_errc::closed,
            .message = "cannot apply the schema to a closed database",
            .sqlite_code = 0,
        });

    for (const auto& sql : schema_statements) {
        if (auto applied = db.execute(sql); !applied)
            return std::unexpected(applied.error());
    }

    auto versioned = has_version(db);
    if (!versioned)
        return std::unexpected(versioned.error());
    if (versioned.value())
        return {};

    // The version row is written on the way in, so a store created before
    // schema_version existed is indistinguishable from an empty one; the file
    // format is new enough that there is nothing to migrate.
    return db.execute(
        std::format("INSERT INTO schema_version (version) VALUES ({})",
                    current_version));
}

}
