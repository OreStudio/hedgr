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
#ifndef HEDGR_STORE_REPOSITORY_MASTER_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_MASTER_REPOSITORY_HPP

#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <string>
#include <string_view>

namespace hedgr::store::repository {

using sqlite::result;

/**
 * @brief Reads and writes the single row that proves the master password.
 *
 * There is no master password anywhere in this class and none in the row it
 * manages. What is stored is a fixed, known sentence sealed under a key derived
 * from the password: an operator unlocks the store by opening that sentence,
 * which can only succeed if the password was right. Nothing anywhere can
 * recover the password, and there is no recovery path by design -- see the
 * @c Master @c password entry in CONTEXT.md.
 *
 * The fixed sentence is a constant in this file and is not a secret. It does
 * not need to be: what makes the check work is that the seal cannot be produced
 * or opened without the password, not that its contents are unknown.
 *
 * The class borrows the connection, so it must not outlive it.
 */
class HEDGR_STORE_EXPORT master_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit master_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c master table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Whether a master password has been created for this store. */
    [[nodiscard]] result<bool> has_master() const;

    /**
     * @brief Seals the verification blob under @p password and stores it.
     *
     * @return An error when the store already has a master password, because
     * replacing one silently would leave every credential sealed under the key
     * that was replaced. @c clear is the deliberate way to start again, and it
     * is the operator's.
     */
    [[nodiscard]] result<void> create(std::string_view password);

    /**
     * @brief Whether @p password opens the stored verification blob.
     *
     * @return @c true when it does, @c false when it does not, and an error
     * only when the check could not be performed at all. A wrong password is
     * not an error here: it is the answer to the question, and the caller
     * renders it as a state rather than reporting it as a failure.
     */
    [[nodiscard]] result<bool> verify(std::string_view password) const;

    /**
     * @brief The sealed verification blob, for a caller that opens it itself.
     *
     * Exposed so the runtime can run the derivation on its own pool and report
     * the outcome as an event. @c verify exists for every caller that does not
     * need that, and is the one to use.
     */
    [[nodiscard]] result<std::string> verification() const;

    /**
     * @brief Removes the master row.
     *
     * Drops the verification blob and nothing else. Every credential sealed
     * under the password that was just forgotten is now unreadable, so this is
     * the last step of a deliberate reset rather than something a failure path
     * may call.
     */
    [[nodiscard]] result<void> clear();

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
