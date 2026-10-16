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
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
#include <sqlgen/delete_from.hpp>
#include <sqlgen/literals.hpp>
#include <sqlgen/order_by.hpp>
#include <sqlgen/read.hpp>
#include <sqlgen/update.hpp>
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

/** @brief The stored row as the domain value the rest of Hedgr speaks. */
dom::instance to_domain(const instance_entity& row) {
    dom::instance value;
    value.id = row.id.get();
    value.name = row.name.get();
    value.icon_key = row.icon_key;
    value.nats_url = row.nats_url;
    value.subject_prefix = row.subject_prefix;
    value.wire_format = row.wire_format;
    value.folder_id = row.folder_id;
    value.tls_ca = row.tls_ca;
    value.tls_cert = row.tls_cert;
    value.tls_key = row.tls_key;
    value.created_at = row.created_at;
    return value;
}

/** @brief The domain value as the entity sqlgen binds. */
instance_entity to_entity(const dom::instance& row) {
    return instance_entity{
        .id = row.id,
        .name = row.name,
        .icon_key = row.icon_key,
        .nats_url = row.nats_url,
        .subject_prefix = row.subject_prefix,
        .wire_format = row.wire_format,
        .folder_id = row.folder_id,
        .tls_ca = row.tls_ca,
        .tls_cert = row.tls_cert,
        .tls_key = row.tls_key,
        .created_at = row.created_at,
    };
}

}

instance_repository::instance_repository(sqlite::database& db) noexcept
    : db_(&db) {}

std::string instance_repository::sql() const {
    return ddl_for(instance_entity::tablename);
}

sqlite::result<std::vector<dom::instance>> instance_repository::list() const {
    const auto query =
        sqlgen::read<std::vector<instance_entity>> | sqlgen::order_by("name"_c);
    return rows_of(query(db_->connection()))
        .transform([](const auto& rows) {
            std::vector<dom::instance> values;
            values.reserve(rows.size());
            for (const auto& row : rows)
                values.push_back(to_domain(row));
            return values;
        });
}

sqlite::result<void> instance_repository::save(const dom::instance& row) {
    auto stored = to_entity(row);

    /*
     * created_at records when the operator first added the instance, and an
     * edit must not move it. The column used to carry a DEFAULT for that, and
     * sqlgen cannot express one, so the writer keeps the value instead: an
     * existing row keeps the stamp it has, and a new row is stamped now.
     */
    if (stored.created_at.empty()) {
        const auto existing = sqlgen::read<std::vector<instance_entity>> |
            sqlgen::where("id"_c == std::string{row.id});
        auto found = rows_of(existing(db_->connection()));
        if (!found)
            return std::unexpected(found.error());
        stored.created_at = found.value().empty()
            ? timestamp_now()
            : found.value().front().created_at;
    }

    return upsert(*db_, stored, {"id"});
}

sqlite::result<void> instance_repository::set_parent(std::string_view id,
    std::string_view folder_id) {
    const auto query =
        sqlgen::update<instance_entity>(
            "folder_id"_c.set(std::string{folder_id})) |
        sqlgen::where("id"_c == std::string{id});
    return done(query(db_->connection()));
}

sqlite::result<void> instance_repository::remove(std::string_view id) {
    const auto query = sqlgen::delete_from<instance_entity> |
        sqlgen::where("id"_c == std::string{id});
    return done(query(db_->connection()));
}

}
