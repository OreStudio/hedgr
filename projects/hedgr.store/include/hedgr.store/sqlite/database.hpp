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
#include <string_view>

struct sqlite3;

namespace hedgr::store::sqlite {

/**
 * @brief An open SQLite database, closed when it goes out of scope.
 *
 * The class owns the connection and nothing else: statements are compiled
 * against it and hold no reference back, so a @c statement must not outlive the
 * @c database it was compiled from.
 *
 * Failure to open is reported rather than thrown. The sqlite3 C API is used
 * directly, so the only shared state between this class and the repositories is
 * the raw handle.
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
     * Use this for pragmas and single-row DDL. Multi-row results and bound
     * parameters belong in @c statement.
     */
    [[nodiscard]] result<void> execute(std::string_view sql);

    /** @brief Brings the store up to the schema this component expects. */
    [[nodiscard]] result<void> apply_schema();

    /** @brief Whether a connection is held and has not been closed. */
    [[nodiscard]] bool is_open() const noexcept;

    /**
     * @brief The raw handle, for compiling statements.
     *
     * This exists for the repositories in this component and for tests that
     * need to plant a row the repositories refuse to write. It is not a public
     * API: nothing outside this component should call it.
     */
    [[nodiscard]] sqlite3* handle() const noexcept;

    /** @brief The file backing the database, empty for an in-memory one. */
    [[nodiscard]] std::filesystem::path path() const;

private:
    /** @brief Adopts an already-open handle and applies the connection pragmas.
     */
    [[nodiscard]] static result<database> adopt(sqlite3* raw);

    explicit database(sqlite3* raw) noexcept;

    sqlite3* handle_ = nullptr;
};

}

#endif
