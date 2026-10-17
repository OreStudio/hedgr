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
#ifndef HEDGR_STORE_REPOSITORY_ACCOUNT_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_ACCOUNT_REPOSITORY_HPP

#include "hedgr.model/domain/account.hpp"
#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

// The repositories speak stored text on one side and domain values on the
// other, so the two namespaces they name are aliased once here.
namespace dom = model::domain;
using sqlite::result;

/**
 * @brief Reads and writes the accounts that may be presented to instances.
 *
 * The repository borrows the connection, so it must not outlive it.
 *
 * An account may carry a credential, but only ever as ciphertext: the
 * credential arrives already sealed and leaves the same way, and no password
 * and no key is ever named in this class. A decrypted secret exists inside the
 * runtime's sign-in attempt and nowhere else, and never passes through here.
 *
 * The @c account table is described by two entity structs rather than one, and
 * the reason is the credential column: @c account_entity does not name it, so
 * no list selects it and no save writes it, and @c account_credential_entity is
 * the one type that does.
 */
class HEDGR_STORE_EXPORT account_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit account_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c account table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Every account, ordered by instance then name. */
    [[nodiscard]] result<std::vector<dom::account>> list() const;

    /** @brief The accounts of one instance, ordered by name. */
    [[nodiscard]] result<std::vector<dom::account>>
    list_for_instance(std::string_view instance_id) const;

    /** @brief Inserts @c row, or replaces the row with the same id. */
    [[nodiscard]] result<void> save(const dom::account& row);

    /** @brief Deletes the account with @c id. */
    [[nodiscard]] result<void> remove(std::string_view id);

    /** @brief Deletes every account of one instance. */
    [[nodiscard]] result<void>
    remove_for_instance(std::string_view instance_id);

    /**
     * @brief Moves the account into @p folder_id, leaving every other column.
     *
     * The counterpart of the instance repository's @c set_parent, and a call of
     * its own for the same reason: a move is a key and a destination.
     */
    [[nodiscard]] result<void>
    set_parent(std::string_view id, std::string_view folder_id);

    /**
     * @brief Stores @p sealed as the account's credential.
     *
     * The value is already sealed by the caller: this class never sees a
     * password and never derives a key, so there is exactly one place in the
     * program where a credential becomes ciphertext.
     */
    [[nodiscard]] result<void>
    set_credential(std::string_view account_id, std::string_view sealed);

    /** @brief Forgets the account's credential, leaving the account. */
    [[nodiscard]] result<void> clear_credential(std::string_view account_id);

    /**
     * @brief The ids of the accounts that have a stored credential.
     *
     * The ids and nothing else. The interface has to know whether pressing Sign
     * in can use a remembered password or has to ask for one, and that is a
     * fact about the row rather than the secret in it. Asking per account with
     * @c credential_of would put every sealed value in memory to answer a
     * question the column itself answers.
     */
    [[nodiscard]] result<std::vector<std::string>> ids_with_credential() const;

    /**
     * @brief The account's sealed credential, or nothing when it has none.
     *
     * Deliberately not part of @c list or @c list_for_instance. A list is for
     * rendering, and a list that carried every credential in the store would
     * put every secret in the process on the path a table draws from -- one
     * forgotten reference away from a window, a diff or a log. A credential is
     * fetched one account at a time, by the one caller that is about to use it.
     *
     * What comes back is still sealed, so even this call hands over nothing
     * that can be read without the master password.
     */
    [[nodiscard]] result<std::optional<std::string>>
    credential_of(std::string_view account_id) const;

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
