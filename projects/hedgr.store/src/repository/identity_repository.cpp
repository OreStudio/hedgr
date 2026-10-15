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
#include "hedgr.store/repository/identity_repository.hpp"
#include "hedgr.store/sqlite/statement.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

constexpr std::string_view select_columns =
    "SELECT id, instance_id, label, principal FROM identity";

dom::identity read_row(const sqlite::statement& query) {
    dom::identity row;
    row.id = std::string(query.column_text(0));
    row.instance_id = std::string(query.column_text(1));
    row.label = std::string(query.column_text(2));
    row.principal = std::string(query.column_text(3));
    return row;
}

sqlite::result<std::vector<dom::identity>>
collect(sqlite::database& db, std::string_view sql, std::string_view argument) {
    sqlite::statement query(db.handle(), sql);
    if (!query.is_valid())
        return std::unexpected(query.step().error());
    if (!argument.empty()) {
        if (auto bound = query.bind(1, argument); !bound)
            return std::unexpected(bound.error());
    }

    std::vector<dom::identity> rows;
    while (true) {
        auto available = query.step();
        if (!available)
            return std::unexpected(available.error());
        if (!available.value())
            break;
        rows.push_back(read_row(query));
    }
    return rows;
}

}

identity_repository::identity_repository(sqlite::database& db) noexcept
    : db_(&db) {}

sqlite::result<std::vector<dom::identity>> identity_repository::list() const {
    const std::string sql =
        std::string(select_columns) + " ORDER BY instance_id, label";
    return collect(*db_, sql, {});
}

sqlite::result<std::vector<dom::identity>>
identity_repository::list_for_instance(std::string_view instance_id) const {
    const std::string sql = std::string(select_columns) +
                            " WHERE instance_id = ? ORDER BY label";
    return collect(*db_, sql, instance_id);
}

sqlite::result<void> identity_repository::save(const dom::identity& row) {
    sqlite::statement insert(
        db_->handle(),
        "INSERT INTO identity (id, instance_id, label, principal) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET instance_id = excluded.instance_id, "
        "label = excluded.label, principal = excluded.principal");
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    if (auto bound = insert.bind(1, row.id); !bound)
        return bound;
    if (auto bound = insert.bind(2, row.instance_id); !bound)
        return bound;
    if (auto bound = insert.bind(3, row.label); !bound)
        return bound;
    if (auto bound = insert.bind(4, row.principal); !bound)
        return bound;

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> identity_repository::remove(std::string_view id) {
    sqlite::statement query(db_->handle(), "DELETE FROM identity WHERE id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> identity_repository::remove_for_instance(
    std::string_view instance_id) {
    sqlite::statement query(db_->handle(),
                            "DELETE FROM identity WHERE instance_id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, instance_id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

}
