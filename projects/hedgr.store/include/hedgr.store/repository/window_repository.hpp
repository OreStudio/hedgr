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
#ifndef HEDGR_STORE_REPOSITORY_WINDOW_REPOSITORY_HPP
#define HEDGR_STORE_REPOSITORY_WINDOW_REPOSITORY_HPP

#include "hedgr.model/domain/window.hpp"
#include "hedgr.store/export.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

// The repositories speak stored text on one side and domain values on the
// other, so the two namespaces they name are aliased once here.
namespace dom = model::domain;
using sqlite::result;

/**
 * @brief Reads and writes the window descriptions that survive a restart.
 *
 * This is the cold half of a window: what it is, not what it is doing now. Live
 * per-frame values live in the model's window table and are never stored.
 *
 * The kind-specific payload is written as JSON beside its kind, through the
 * model's @c window_state_to_json, so no @c std::variant is ever serialised and
 * adding a window kind needs no change here.
 *
 * The repository borrows the connection, so it must not outlive it.
 */
class HEDGR_STORE_EXPORT window_repository final {
public:
    /** @brief Binds the repository to an open @c db. */
    explicit window_repository(sqlite::database& db) noexcept;

    /** @brief The DDL this build applies for the @c window_spec table. */
    [[nodiscard]] std::string sql() const;

    /** @brief Every stored window, ordered by id. */
    [[nodiscard]] result<std::vector<dom::window_spec>> list() const;

    /** @brief Inserts @c spec, or replaces the window with the same id. */
    [[nodiscard]] result<void> save(const dom::window_spec& spec);

    /** @brief Deletes the window with @c window_id. */
    [[nodiscard]] result<void> remove(std::string_view window_id);

private:
    sqlite::database* db_ = nullptr;
};

}

#endif
