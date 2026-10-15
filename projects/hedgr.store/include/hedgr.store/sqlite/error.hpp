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
#ifndef HEDGR_STORE_SQLITE_ERROR_HPP
#define HEDGR_STORE_SQLITE_ERROR_HPP

#include "hedgr.store/export.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::store::sqlite {

/**
 * @brief The closed set of ways a store operation can fail.
 *
 * The set is closed so callers switch on it instead of inspecting message text,
 * which is free-form and carries the row that failed.
 */
enum class store_errc : std::uint8_t {
    /** @brief The database file could not be created or opened. */
    open_failed,
    /** @brief A statement could not be compiled. */
    prepare_failed,
    /** @brief A parameter could not be attached to a statement. */
    bind_failed,
    /** @brief Advancing a statement failed. */
    step_failed,
    /** @brief A uniqueness or foreign-key constraint rejected the write. */
    constraint_violated,
    /** @brief The requested row does not exist. */
    not_found,
    /** @brief A stored value could not be parsed back into its domain type. */
    corrupt_row,
    /**
     * @brief The file was written against a different schema version.
     *
     * A distinct code rather than a generic failure, because the cause is the
     * file rather than the operation and the remedy is the operator's: the
     * store is pre-release, so the file is to be deleted and rebuilt. Nothing
     * in the application deletes it, so a caller that sees this must never
     * treat it as retryable.
     */
    schema_mismatch,
    /** @brief The operation needed an open database and did not get one. */
    closed,
    /**
     * @brief The cryptography that guards a credential could not run.
     *
     * Distinct from @c wrong_master_password because it is not the operator's
     * doing: no password they could type would change the outcome, and the
     * message beside it describes a broken OpenSSL step rather than a refusal.
     */
    crypto_failed,
    /**
     * @brief The master password did not open the store.
     *
     * The one credential failure an operator can act on by typing something
     * else, which is why it is the one that reaches a prompt as a sentence
     * rather than as an internal error.
     */
    wrong_master_password,
};

/**
 * @brief Why a store operation failed.
 *
 * The message names the row or statement that failed so a log line points at
 * the offending data without a debugger. @c sqlite_code is kept beside the
 * classified code because the classification loses the distinction between,
 * for example, two different constraint failures.
 */
struct store_error final {
    /** @brief The classified failure. */
    store_errc code = store_errc::closed;

    /** @brief Human-readable detail, naming the offending row where known. */
    std::string message;

    /** @brief The underlying SQLITE_* result code, 0 when not applicable. */
    int sqlite_code = 0;
};

/**
 * @brief The outcome of a store operation.
 *
 * Every fallible entry point in this component returns one of these, so no
 * SQLITE_* result code and no exception crosses the component boundary.
 */
template <typename T>
using result = std::expected<T, store_error>;

/** @brief The stable text of a failure code, for logs and test messages. */
[[nodiscard]] HEDGR_STORE_EXPORT std::string_view
to_string(store_errc value) noexcept;

}

#endif
