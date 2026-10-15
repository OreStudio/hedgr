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
#include "hedgr.store/repository/folder_repository.hpp"
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
#include <format>
#include <sqlgen/delete_from.hpp>
#include <sqlgen/literals.hpp>
#include <sqlgen/order_by.hpp>
#include <sqlgen/read.hpp>
#include <sqlgen/where.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

using namespace sqlgen::literals;

sqlite::store_error corrupt_row(std::string_view id, std::string_view detail) {
    return sqlite::store_error{
        .code = sqlite::store_errc::corrupt_row,
        .message = std::format("folder {} is malformed: {}", id, detail),
        .sqlite_code = 0,
    };
}

/** @brief The stored row as a domain value, or why it is not one. */
sqlite::result<dom::folder> to_domain(const folder_entity& row) {
    dom::folder value;
    value.id = row.id.get();
    value.parent_id = row.parent_id;
    value.owner_id = row.owner_id;
    value.name = row.name;

    const auto tree = dom::folder_tree_from_string(row.tree);
    if (!tree) {
        return std::unexpected(corrupt_row(
            value.id, std::format("unknown tree [{}]: {}", row.tree, tree.error())));
    }
    value.tree = tree.value();
    return value;
}

/** @brief The domain value as the entity sqlgen binds. */
folder_entity to_entity(const dom::folder& row) {
    return folder_entity{
        .id = row.id,
        .parent_id = row.parent_id,
        .owner_id = row.owner_id,
        .tree = std::string{dom::to_string(row.tree)},
        .name = row.name,
    };
}

}

folder_repository::folder_repository(sqlite::database& db) noexcept
    : db_(&db) {}

std::string folder_repository::sql() const {
    return ddl_for(folder_entity::tablename);
}

sqlite::result<std::vector<dom::folder>> folder_repository::list() const {
    const auto query = sqlgen::read<std::vector<folder_entity>> |
        sqlgen::order_by("tree"_c, "owner_id"_c, "name"_c);

    auto rows = rows_of(query(db_->connection()));
    if (!rows)
        return std::unexpected(rows.error());

    std::vector<dom::folder> values;
    values.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        auto value = to_domain(row);
        if (!value)
            return std::unexpected(value.error());
        values.push_back(std::move(value).value());
    }
    return values;
}

sqlite::result<void> folder_repository::save(const dom::folder& row) {
    return upsert(*db_, to_entity(row), {"id"});
}

sqlite::result<void> folder_repository::remove(std::string_view id) {
    const auto query = sqlgen::delete_from<folder_entity> |
        sqlgen::where("id"_c == std::string{id});
    return done(query(db_->connection()));
}

sqlite::result<void> folder_repository::remove_tree(dom::folder_tree tree,
    std::string_view owner_id) {
    const auto query = sqlgen::delete_from<folder_entity> |
        sqlgen::where("tree"_c == std::string{dom::to_string(tree)} &&
                      "owner_id"_c == std::string{owner_id});
    return done(query(db_->connection()));
}

}
