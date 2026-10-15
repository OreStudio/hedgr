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
#ifndef HEDGR_STORE_REPOSITORY_IDENTITY_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_IDENTITY_REPOSITORY_HPP

#include "hedgr.model/domain/identity.hpp"
#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

// The repositories speak stored text on one side and domain values on the
// other, so the two namespaces they name are aliased once here.
namespace dom = model::domain;
using sqlite::result;

/**
 * @brief Reads and writes the identities that may be presented to instances.
 *
 * The repository borrows the connection, so it must not outlive it. Nothing
 * here relates to a credential: an identity is a principal, and the secret that
 * authenticates it lives in memory for one connect attempt only.
 */
class HEDGR_STORE_EXPORT identity_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit identity_repository(sqlite::database& db) noexcept;

    /** @brief Every identity, ordered by instance then label. */
    [[nodiscard]] result<std::vector<dom::identity>> list() const;

    /** @brief The identities of one instance, ordered by label. */
    [[nodiscard]] result<std::vector<dom::identity>>
    list_for_instance(std::string_view instance_id) const;

    /** @brief Inserts @c row, or replaces the row with the same id. */
    [[nodiscard]] result<void> save(const dom::identity& row);

    /** @brief Deletes the identity with @c id. */
    [[nodiscard]] result<void> remove(std::string_view id);

    /** @brief Deletes every identity of one instance. */
    [[nodiscard]] result<void>
    remove_for_instance(std::string_view instance_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
