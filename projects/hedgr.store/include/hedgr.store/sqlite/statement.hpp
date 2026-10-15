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
#ifndef HEDGR_STORE_SQLITE_STATEMENT_HPP
#define HEDGR_STORE_SQLITE_STATEMENT_HPP

#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <cstdint>
#include <expected>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace hedgr::store::sqlite {

/**
 * @brief A compiled SQL statement, finalized when it goes out of scope.
 *
 * Compilation happens in the constructor rather than in a factory, so a
 * repository can hold a statement as a member without an optional. A statement
 * that failed to compile is invalid and every operation on it fails with the
 * original error, which keeps the caller's happy path free of a validity check.
 *
 * The statement borrows the connection: it must not outlive the @c database it
 * was compiled from.
 */
class HEDGR_STORE_EXPORT statement final {
public:
    /** @brief Compiles @c sql against @c db. */
    statement(sqlite3* db, std::string_view sql);

    /** @brief Finalizes the statement if it is still alive. */
    ~statement();

    /** @brief Move construction transfers ownership of the compiled statement.
     */
    statement(statement&& other) noexcept;

    /** @brief Move assignment finalizes the current statement, then takes the
     * other one. */
    statement& operator=(statement&& other) noexcept;

    statement(const statement&) = delete;
    statement& operator=(const statement&) = delete;

    /** @brief Whether compilation succeeded. */
    [[nodiscard]] bool is_valid() const noexcept;

    /** @brief Binds text at 1-based @c index, copying the bytes. */
    [[nodiscard]] result<void> bind(int index, std::string_view value);

    /** @brief Binds an integer at 1-based @c index. */
    [[nodiscard]] result<void> bind(int index, std::int64_t value);

    /** @brief Binds SQL NULL at 1-based @c index. */
    [[nodiscard]] result<void> bind_null(int index);

    /**
     * @brief Advances the statement.
     *
     * Returns true when a row is available, false when the statement is done.
     * Anything else is a failure, and a constraint violation keeps its own code
     * because it is the one failure a caller can be expected to act on.
     */
    [[nodiscard]] result<bool> step();

    /** @brief Zero-based @c column as text; empty for SQL NULL. */
    [[nodiscard]] std::string_view column_text(int column) const;

    /** @brief Zero-based @c column as an integer; 0 for SQL NULL. */
    [[nodiscard]] std::int64_t column_int64(int column) const;

    /** @brief Whether zero-based @c column holds SQL NULL. */
    [[nodiscard]] bool is_null(int column) const noexcept;

    /** @brief Clears bindings and rewinds, so the statement can run again. */
    [[nodiscard]] result<void> reset();

private:
    /** @brief The stored compilation failure, or a success. */
    result<void> prepared_;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* handle_ = nullptr;
};

}

#endif
