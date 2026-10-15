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
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace hedgr::store::repository {

namespace {

/**
 * @brief The schema version written and expected by this component.
 *
 * Bumped from 1 when an instance row stopped carrying a safety classification
 * and the accounts table took its current name, from 2 when the folders, labels
 * and label attachments were added and an instance stopped carrying a
 * comma-separated label string, and from 3 when an account's free-text `label`
 * column became `name` so that it no longer shares a word with the label
 * entity. The store is pre-release, so there is no migration: a file written
 * against a different version is refused, and the operator deletes it.
 */
constexpr std::int64_t current_version = 3;

/**
 * @brief The whole schema, in one place.
 *
 * The tables mirror the domain types rather than any view's needs: an instance
 * carries its transport, an account carries no credential, and a window carries
 * its kind beside its payload so no variant is ever written out.
 *
 * @c window_spec.session_instance_id and @c window_spec.session_account_id are
 * nullable because a window may be bound to no session at all, which is the
 * normal case for the instance manager.
 */
constexpr std::array<std::string_view, 10> schema_statements{
    R"(PRAGMA foreign_keys = ON)",
    R"(CREATE TABLE IF NOT EXISTS instance (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        nats_url TEXT NOT NULL,
        subject_prefix TEXT NOT NULL,
        wire_format TEXT NOT NULL DEFAULT 'msgpack',
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    ))",
    R"(CREATE TABLE IF NOT EXISTS account (
        id TEXT PRIMARY KEY,
        instance_id TEXT NOT NULL REFERENCES instance(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        principal TEXT NOT NULL,
        UNIQUE(instance_id, principal)
    ))",
    /*
     * A folder is a node of one of two trees, so it records which tree and, for
     * an accounts tree, the instance that owns it. The partial unique index is
     * what makes the single-root rule a database fact rather than a convention:
     * at most one row per (tree, owner) may have no parent, so a second root in
     * the instances tree, or a second accounts root for one instance, is
     * refused by SQLite even if a caller never checks first.
     */
    R"(CREATE TABLE IF NOT EXISTS folder (
        id TEXT PRIMARY KEY,
        parent_id TEXT NOT NULL DEFAULT '',
        owner_id TEXT NOT NULL DEFAULT '',
        tree TEXT NOT NULL,
        name TEXT NOT NULL
    ))",
    R"(CREATE UNIQUE INDEX IF NOT EXISTS folder_root ON folder(tree, owner_id)
        WHERE parent_id = '')",
    R"(CREATE TABLE IF NOT EXISTS label (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        colour TEXT NOT NULL,
        severity TEXT NOT NULL
    ))",
    /*
     * The two attachment tables are separate rather than one table with an
     * owner-kind column, because each foreign key then names the table it
     * actually references and SQLite can cascade on it. A removal of an
     * instance or an account therefore takes its attachments with it without
     * the runtime having to walk the junction.
     */
    R"(CREATE TABLE IF NOT EXISTS instance_label (
        instance_id TEXT NOT NULL REFERENCES instance(id) ON DELETE CASCADE,
        label_id TEXT NOT NULL REFERENCES label(id) ON DELETE CASCADE,
        PRIMARY KEY (instance_id, label_id)
    ))",
    R"(CREATE TABLE IF NOT EXISTS account_label (
        account_id TEXT NOT NULL REFERENCES account(id) ON DELETE CASCADE,
        label_id TEXT NOT NULL REFERENCES label(id) ON DELETE CASCADE,
        PRIMARY KEY (account_id, label_id)
    ))",
    R"(CREATE TABLE IF NOT EXISTS window_spec (
        window_id TEXT PRIMARY KEY,
        kind TEXT NOT NULL,
        session_instance_id TEXT,
        session_account_id TEXT,
        cadence_ms INTEGER NOT NULL DEFAULT 1000,
        state_json TEXT NOT NULL DEFAULT '{}'
    ))",
    R"(CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER NOT NULL
    ))",
};

/**
 * @brief The version the file was written with, when it records one.
 *
 * The newest row wins rather than the only one: the table should hold a single
 * row, but a file that somehow accreted more must not be read as whatever
 * SQLite happens to return first.
 */
result<std::optional<std::int64_t>> stored_version(sqlite::database& db) {
    sqlite::statement query(
        db.handle(),
        "SELECT version FROM schema_version ORDER BY version DESC LIMIT 1");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    auto row = query.step();
    if (!row)
        return std::unexpected(row.error());
    if (!row.value())
        return std::optional<std::int64_t>{};
    return std::optional<std::int64_t>{query.column_int64(0)};
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
