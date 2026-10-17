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
#include "ores.logging/make_logger.hpp"
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

/** @brief This store's logger. The schema and its version are what it names. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.store.repository.schema");
    return instance;
}


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
 * instance gained its own TLS material as three paths. It goes to 8 with the
 * icon key an instance names its illustration by and the shortcut table the
 * shortcuts bar is kept in. A file written by version 7 is migrated forward by
 * the step below, and a file from any other version is refused.
 */
constexpr std::int64_t current_version = 8;

/**
 * @brief The one older version this build knows how to bring forward.
 *
 * Only the step immediately below the current version has a migration. The
 * difference between a version 7 file and this build is one empty column on
 * @c instance and one new table, and both are stated exactly rather than
 * guessed, so the step is safe to run on an operator's rows. A version below 7
 * predates changes this code no longer records, and a migration invented from
 * the current end state would silently rewrite rows whose old shape nobody can
 * still confirm. Version 7 is therefore the cut-off. It is migrated, anything
 * older than it and anything newer than this build is refused, and the sentence
 * returned says so.
 */
constexpr std::int64_t migrated_from_version = 7;

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
 * @brief The failure that tells the operator to deal with the file by hand.
 *
 * The path, both versions and the remedy are all in the sentence, because this
 * is the one store failure a person has to act on by hand and it reaches them
 * through a window with room for a line or two. It is deliberately not a
 * "retry" or an "internal error", because nothing in the application can fix
 * it. The version this build migrates from is named too, so an operator holding
 * an older file can see that 7 is the one that would have opened and that
 * theirs is past the point the store can bring forward.
 */
sqlite::store_error version_mismatch(const std::filesystem::path& path,
    std::int64_t stored) {
    return sqlite::store_error{
        .code = sqlite::store_errc::schema_mismatch,
        .message = std::format(
            "the store at {} was written with schema version {} but this build "
            "expects version {}; this build migrates a version {} store "
            "forward, and one older than that or newer than this build cannot "
            "be migrated, so delete the file and start again",
            path.string(), stored, current_version, migrated_from_version),
        .sqlite_code = 0,
    };
}

/**
 * @brief Starts the transaction the migration runs in, or says why it cannot.
 *
 * sqlgen's connection carries the transaction calls and @c database does not
 * re-export them, so this is where they are called and where their rfl error is
 * folded into the store's own, exactly as @c database::execute does for a
 * statement. The connection is reached directly because this file is inside the
 * component that owns it.
 */
result<void> begin_transaction(sqlite::database& db) {
    auto begun = db.connection()->begin_transaction();
    if (!begun)
        return std::unexpected(
            sqlite::from_sqlgen(begun.error(), sqlite::store_errc::step_failed));
    return {};
}

/**
 * @brief Brings a version 7 file to the version 8 shape without losing a row.
 *
 * Two changes make the difference. @c instance gains @c icon_key, empty on
 * every existing row, and @c shortcut is created. The create statement comes
 * from @c ddl_for, which is the same statement the fresh-create path applies,
 * so the table cannot end up with a shape the repository that binds it does not
 * expect.
 *
 * Every existing row gets the empty key, because empty is the stored meaning of
 * no illustration chosen. The seeder's rows are derivable, because it names an
 * instance @c inst-env-<label> after the environment it found, so a key could
 * be read off the name. It is not done, because the store cannot tell a row the
 * seeder wrote from one an operator typed with a name of the same shape, and a
 * key invented for the wrong row is a picture the operator never chose.
 *
 * The whole migration runs in one transaction. SQLite makes DDL transactional,
 * so a failure after the column was added puts the file back exactly as it was
 * and leaves the version row saying 7. That is what makes the next open retry
 * the whole migration rather than trust a shape that is neither version.
 */
result<void> migrate_seven_to_eight(sqlite::database& db) {
    if (auto started = begin_transaction(db); !started)
        return std::unexpected(started.error());

    /*
     * Roll the file back, then hand back the failure that stopped the
     * migration. The rollback's own outcome is discarded on purpose, because
     * the first failure is the one worth reporting, and a rollback that itself
     * failed has left the transaction to the close of the connection.
     */
    const auto abandon = [&db](sqlite::store_error error) {
        (void)db.connection()->rollback();
        return std::unexpected(std::move(error));
    };

    /*
     * SQLite refuses a NOT NULL column added to an existing table unless it
     * carries a non-null default, so the empty string is stated here as well as
     * being the value every existing row takes from it.
     */
    if (auto added = db.execute(
            "ALTER TABLE instance ADD COLUMN icon_key TEXT NOT NULL DEFAULT ''");
        !added)
        return abandon(added.error());

    const auto shortcut = ddl_for(shortcut_entity::tablename);
    if (shortcut.empty()) {
        return abandon(sqlite::store_error{
            .code = sqlite::store_errc::schema_mismatch,
            .message = "the shortcut table has no definition to create, so the "
                       "version 7 store cannot be migrated",
            .sqlite_code = 0,
        });
    }
    if (auto created = db.execute(shortcut); !created)
        return abandon(created.error());

    if (auto stamped = db.execute(std::format(
            "UPDATE schema_version SET version = {}", current_version));
        !stamped)
        return abandon(stamped.error());

    auto committed = db.connection()->commit();
    if (!committed)
        return std::unexpected(
            sqlite::from_sqlgen(committed.error(), sqlite::store_errc::step_failed));
    return {};
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
     * The version table is created on its own before anything else, because the
     * version has to be known before the rest of the schema is applied. A file
     * written by version 7 must be migrated rather than merely met with the
     * IF NOT EXISTS statements below, which would leave its old instance table
     * alone. The table has no history, so creating it on any file is safe, and
     * on a fresh file it is what gives the read below an empty table to read.
     */
    if (auto created = db.execute(ddl_for(schema_version_entity::tablename));
        !created)
        return std::unexpected(created.error());

    /*
     * Reading the version is what notices that a file is stale, and it happens
     * before the rest of the schema so the file can be migrated rather than
     * diagnosed. Without this, an old database opened fine against the
     * IF NOT EXISTS statements and then failed somewhere far from the cause,
     * once per query, with a message about whichever column the current build
     * happens to name first.
     */
    auto stored = stored_version(db);
    if (!stored)
        return std::unexpected(stored.error());

    if (!stored.value()) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Creating a new store at " << db.path().string()
            << " at schema version " << current_version;
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Store at " << db.path().string() << " is at schema version "
            << stored.value().value() << ", expected " << current_version;
    }

    if (stored.value() && stored.value().value() != current_version) {
        if (stored.value().value() != migrated_from_version) {
            BOOST_LOG_SEV(lg(), ores::logging::error)
                << "Refusing a store at version " << stored.value().value()
                << ": this build expects version " << current_version;
            return std::unexpected(
                version_mismatch(db.path(), stored.value().value()));
        }
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Migrating the store from version " << migrated_from_version
            << " to " << current_version;
        if (auto migrated = migrate_seven_to_eight(db); !migrated)
            return std::unexpected(migrated.error());
    }

    /*
     * The tables are created from the entity structs, so a column renamed in
     * one place cannot survive in the other: the statement that creates it and
     * the code that binds it are two readings of one declaration. The three
     * tables that carry a cascade foreign key, and the two composite keys that
     * come with them, are the exceptions and are listed with their reason in
     * ddl.cpp. Every statement is IF NOT EXISTS, so on a file that was just
     * migrated this fills in whatever the migration did not recreate.
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
     * No stored version means a file with no history. Since every version this
     * store has ever written wrote a row, the only way to reach this is a file
     * created empty by this call, so stamping the current version is right. A
     * file from before schema_version existed cannot be told apart from an
     * empty one, which is the case the entry above refuses for every version
     * that can. A file that was migrated already carries the current stamp, so
     * there is nothing to write.
     */
    if (stored.value())
        return {};

    return db.execute(
        std::format("INSERT INTO schema_version (version) VALUES ({})",
                    current_version));
}

}
