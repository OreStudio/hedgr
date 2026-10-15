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
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
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

/**
 * @brief Every instance attachment, in the order the list reports them.
 *
 * The junction lives in two tables because each foreign key has to name the
 * table it cascades on, so every operation here is written twice, once per
 * owner kind. That duplication is the price of the cascade and it is confined
 * to this file.
 */
sqlite::result<std::vector<dom::labelling>> list_instances(
    sqlite::database& db) {
    const auto query = sqlgen::read<std::vector<instance_label_entity>> |
        sqlgen::order_by("instance_id"_c, "label_id"_c);

    auto rows = rows_of(query(db.connection()));
    if (!rows)
        return std::unexpected(rows.error());

    std::vector<dom::labelling> values;
    values.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        values.push_back(dom::labelling{
            .owner = dom::labelling_owner::instance,
            .owner_id = row.instance_id,
            .label_id = row.label_id,
        });
    }
    return values;
}

/** @brief Every account attachment, in the order the list reports them. */
sqlite::result<std::vector<dom::labelling>> list_accounts(
    sqlite::database& db) {
    const auto query = sqlgen::read<std::vector<account_label_entity>> |
        sqlgen::order_by("account_id"_c, "label_id"_c);

    auto rows = rows_of(query(db.connection()));
    if (!rows)
        return std::unexpected(rows.error());

    std::vector<dom::labelling> values;
    values.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        values.push_back(dom::labelling{
            .owner = dom::labelling_owner::account,
            .owner_id = row.account_id,
            .label_id = row.label_id,
        });
    }
    return values;
}

}

labelling_repository::labelling_repository(sqlite::database& db) noexcept
    : db_(&db) {}

std::string labelling_repository::sql() const {
    return ddl_for(instance_label_entity::tablename) + "\n" +
        ddl_for(account_label_entity::tablename);
}

sqlite::result<std::vector<dom::labelling>> labelling_repository::list() const {
    auto instances = list_instances(*db_);
    if (!instances)
        return std::unexpected(instances.error());

    auto accounts = list_accounts(*db_);
    if (!accounts)
        return std::unexpected(accounts.error());

    auto values = std::move(instances).value();
    auto more = std::move(accounts).value();
    values.insert(values.end(), more.begin(), more.end());
    return values;
}

sqlite::result<void> labelling_repository::save(const dom::labelling& row) {
    if (row.owner == dom::labelling_owner::account) {
        return upsert(*db_,
            account_label_entity{
                .account_id = row.owner_id,
                .label_id = row.label_id,
            },
            {"account_id", "label_id"});
    }
    return upsert(*db_,
        instance_label_entity{
            .instance_id = row.owner_id,
            .label_id = row.label_id,
        },
        {"instance_id", "label_id"});
}

sqlite::result<void> labelling_repository::remove(const dom::labelling& row) {
    if (row.owner == dom::labelling_owner::account) {
        const auto query = sqlgen::delete_from<account_label_entity> |
            sqlgen::where("account_id"_c == std::string{row.owner_id} &&
                          "label_id"_c == std::string{row.label_id});
        return done(query(db_->connection()));
    }
    const auto query = sqlgen::delete_from<instance_label_entity> |
        sqlgen::where("instance_id"_c == std::string{row.owner_id} &&
                      "label_id"_c == std::string{row.label_id});
    return done(query(db_->connection()));
}

sqlite::result<void> labelling_repository::remove_owner(
    dom::labelling_owner owner, std::string_view owner_id) {
    if (owner == dom::labelling_owner::account) {
        const auto query = sqlgen::delete_from<account_label_entity> |
            sqlgen::where("account_id"_c == std::string{owner_id});
        return done(query(db_->connection()));
    }
    const auto query = sqlgen::delete_from<instance_label_entity> |
        sqlgen::where("instance_id"_c == std::string{owner_id});
    return done(query(db_->connection()));
}

}
