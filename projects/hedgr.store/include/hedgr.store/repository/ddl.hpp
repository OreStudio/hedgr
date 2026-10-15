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
#ifndef HEDGR_STORE_REPOSITORY_DDL_HPP
#define HEDGR_STORE_REPOSITORY_DDL_HPP

#include "hedgr.store/export.hpp"
#include <sqlgen/create_table.hpp>
#include <sqlgen/if_not_exists.hpp>
#include <sqlgen/sqlite.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::store::repository {

/**
 * @brief The @c CREATE @c TABLE statement sqlgen generates from @p Entity.
 *
 * This is the whole point of describing a table as a struct: the statement that
 * creates the columns and the code that binds them are two readings of one
 * declaration, so they cannot disagree about a column without failing to
 * compile.
 */
template <class Entity>
[[nodiscard]] std::string create_table_sql() {
    return sqlgen::sqlite::to_sql(
        sqlgen::create_table<Entity> | sqlgen::if_not_exists);
}

/** @brief One table's definition, its name, and the order it is created in. */
struct table_ddl final {
    /** @brief The table's name, spelled as the repositories spell it. */
    std::string_view name;

    /** @brief The statement that creates it. */
    std::string text;
};

/**
 * @brief Every table this build applies, in the order they are created.
 *
 * The order matters: a table that references another has to come second, and
 * the schema is applied from this list rather than from a second list written
 * beside it.
 */
[[nodiscard]] HEDGR_STORE_EXPORT const std::vector<table_ddl>& tables();

/**
 * @brief The definition of @p table, or an empty string when none is known.
 *
 * Read by each repository's @c sql(), so what a repository reports it needs is
 * the statement the schema actually applies rather than a second copy of it.
 */
[[nodiscard]] HEDGR_STORE_EXPORT std::string
ddl_for(std::string_view table);

/**
 * @brief The indexes, which are constraints and are not part of a table.
 *
 * Generated from the same entity structs as the tables, for the same reason.
 */
[[nodiscard]] HEDGR_STORE_EXPORT const std::vector<std::string>& indexes();

}

#endif
