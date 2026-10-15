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
#ifndef HEDGR_STORE_SQLITE_DATABASE_HPP
#define HEDGR_STORE_SQLITE_DATABASE_HPP

#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <expected>
#include <filesystem>
#include <sqlgen/sqlite.hpp>
#include <string_view>

namespace hedgr::store::sqlite {

/**
 * @brief An open SQLite database, closed when it goes out of scope.
 *
 * The class owns the one connection to the file and nothing else. There is
 * exactly one, deliberately: @c PRAGMA @c foreign_keys is per-connection in
 * SQLite, so a second handle opened behind this one would not enforce the
 * cascades the schema declares, and a delete would silently leave behind the
 * rows it was supposed to take with it.
 *
 * The connection is sqlgen's, because the repositories read and write through
 * it. sqlgen's own error channel is @c rfl::Result, and this class and the
 * repositories translate it at the boundary: nothing outside this component
 * sees anything but @c std::expected and @c store_error.
 */
class HEDGR_STORE_EXPORT database final {
public:
    /** @brief Closes the connection if it is still open. */
    ~database();

    /** @brief Move construction transfers ownership of the connection. */
    database(database&& other) noexcept;

    /** @brief Move assignment closes the current connection, then takes the
     * other one. */
    database& operator=(database&& other) noexcept;

    database(const database&) = delete;
    database& operator=(const database&) = delete;

    /**
     * @brief Opens, creating missing parent directories and the file itself.
     *
     * Foreign keys are enabled and the journal is put in WAL mode, because the
     * store must survive a client that is killed mid-write and because
     * repository writes outlive the process that made them.
     */
    [[nodiscard]] static result<database>
    open(const std::filesystem::path& path);

    /** @brief Opens a private in-memory database, for tests. */
    [[nodiscard]] static result<database> open_in_memory();

    /**
     * @brief Runs one statement and discards its rows.
     *
     * Use this for pragmas and DDL. Reading rows belongs in a repository, which
     * maps them through an entity rather than by column position.
     */
    [[nodiscard]] result<void> execute(std::string_view sql);

    /** @brief Brings the store up to the schema this component expects. */
    [[nodiscard]] result<void> apply_schema();

    /** @brief Whether a connection is held and has not been closed. */
    [[nodiscard]] bool is_open() const noexcept;

    /**
     * @brief The open connection, for the repositories in this component.
     *
     * This is what replaced the raw @c sqlite3* handle the repositories used to
     * compile statements against. It is not a public API: nothing outside this
     * component should call it.
     */
    [[nodiscard]] const sqlgen::Ref<sqlgen::sqlite::Connection>&
    connection() const noexcept;

    /** @brief The file backing the database, empty for an in-memory one. */
    [[nodiscard]] std::filesystem::path path() const;

private:
    /** @brief Adopts an already-open connection and applies its pragmas. */
    [[nodiscard]] static result<database>
    adopt(const sqlgen::Ref<sqlgen::sqlite::Connection>& raw,
          std::filesystem::path path);

    explicit database(sqlgen::Ref<sqlgen::sqlite::Connection> raw,
                      std::filesystem::path path) noexcept;

    sqlgen::Ref<sqlgen::sqlite::Connection> handle_;
    std::filesystem::path path_;
};

/**
 * @brief A sqlgen failure as a store failure.
 *
 * sqlgen reports what went wrong as a sentence and nothing else, so the closed
 * code is decided here from the sentence where SQLite names a constraint and
 * defaults to @p fallback otherwise. @c store_error::sqlite_code stays 0
 * because sqlgen does not carry it: a code invented here would be worse than an
 * absent one.
 *
 * The message SQLite writes for a refused write always begins with the kind of
 * constraint that was refused -- @c "UNIQUE constraint failed: ..." , @c
 * "FOREIGN KEY constraint failed" -- which is the only signal there is.
 */
[[nodiscard]] HEDGR_STORE_EXPORT store_error
from_sqlgen(const rfl::Error& error, store_errc fallback);

}

#endif
