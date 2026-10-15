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
#include "hedgr.store/repository/labelling_repository.hpp"
#include "hedgr.store/sqlite/statement.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

/**
 * @brief The two statements a labelling needs, one per owner kind.
 *
 * The attachment lives in two tables because each foreign key has to name the
 * table it cascades on, so every operation repeats itself over the pair. The
 * pair is spelled once here and the operations loop over it, which is the one
 * place the two tables are visible.
 */
struct junction final {
    /** @brief Which owner kind this table holds. */
    dom::labelling_owner owner = dom::labelling_owner::instance;

    /** @brief The table's name. */
    std::string_view table;

    /** @brief The table's owner column name. */
    std::string_view owner_column;

    /** @brief The statement attaching a pair. */
    std::string_view insert_sql;

    /** @brief The statement detaching a pair. */
    std::string_view delete_sql;

    /** @brief The statement detaching every label of one owner. */
    std::string_view delete_owner_sql;
};

constexpr junction junctions[] = {
    {
        .owner = dom::labelling_owner::instance,
        .table = "instance_label",
        .owner_column = "instance_id",
        .insert_sql =
            "INSERT INTO instance_label (instance_id, label_id) VALUES (?, ?) "
            "ON CONFLICT(instance_id, label_id) DO NOTHING",
        .delete_sql =
            "DELETE FROM instance_label WHERE instance_id = ? AND label_id = ?",
        .delete_owner_sql =
            "DELETE FROM instance_label WHERE instance_id = ?",
    },
    {
        .owner = dom::labelling_owner::account,
        .table = "account_label",
        .owner_column = "account_id",
        .insert_sql =
            "INSERT INTO account_label (account_id, label_id) VALUES (?, ?) "
            "ON CONFLICT(account_id, label_id) DO NOTHING",
        .delete_sql =
            "DELETE FROM account_label WHERE account_id = ? AND label_id = ?",
        .delete_owner_sql =
            "DELETE FROM account_label WHERE account_id = ?",
    },
};

/** @brief The junction table holding this owner kind's attachments. */
[[nodiscard]] const junction& junction_for(dom::labelling_owner owner) {
    return owner == dom::labelling_owner::account ? junctions[1] : junctions[0];
}

/** @brief The statement that lists one table's attachments. */
[[nodiscard]] std::string select_sql(const junction& side) {
    return "SELECT " + std::string{side.owner_column} + ", label_id FROM "
        + std::string{side.table} + " ORDER BY " + std::string{side.owner_column}
        + ", label_id";
}

}

labelling_repository::labelling_repository(sqlite::database& db) noexcept
    : db_(&db) {}

sqlite::result<std::vector<dom::labelling>> labelling_repository::list() const {
    std::vector<dom::labelling> rows;
    for (const auto& side : junctions) {
        sqlite::statement query(db_->handle(), select_sql(side));
        if (!query.is_valid())
            return std::unexpected(query.step().error());

        while (true) {
            auto available = query.step();
            if (!available)
                return std::unexpected(available.error());
            if (!available.value())
                break;

            dom::labelling row;
            row.owner = side.owner;
            row.owner_id = std::string(query.column_text(0));
            row.label_id = std::string(query.column_text(1));
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

sqlite::result<void> labelling_repository::save(const dom::labelling& row) {
    const auto& side = junction_for(row.owner);
    sqlite::statement insert(db_->handle(), side.insert_sql);
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    if (auto bound = insert.bind(1, row.owner_id); !bound)
        return bound;
    if (auto bound = insert.bind(2, row.label_id); !bound)
        return bound;

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> labelling_repository::remove(const dom::labelling& row) {
    const auto& side = junction_for(row.owner);
    sqlite::statement query(db_->handle(), side.delete_sql);
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, row.owner_id); !bound)
        return bound;
    if (auto bound = query.bind(2, row.label_id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> labelling_repository::remove_owner(
    dom::labelling_owner owner, std::string_view owner_id) {
    const auto& side = junction_for(owner);
    sqlite::statement query(db_->handle(), side.delete_owner_sql);
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, owner_id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

}
