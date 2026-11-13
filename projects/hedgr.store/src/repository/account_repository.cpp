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
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
#include <format>
#include <optional>
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
dom::account to_domain(const account_entity& row) {
    dom::account value;
    value.id = row.id.get();
    value.instance_id = row.instance_id;
    value.name = row.name;
    value.principal = row.principal;
    value.folder_id = row.folder_id;
    value.party_id = row.party_id;
    return value;
}

/** @brief The domain value as the entity sqlgen binds. */
account_entity to_entity(const dom::account& row) {
    return account_entity{
        .id = row.id,
        .instance_id = row.instance_id,
        .name = row.name,
        .principal = row.principal,
        .folder_id = row.folder_id,
        .party_id = row.party_id,
    };
}

/**
 * @brief The outcome of a credential UPDATE, or @c not_found when no account
 * holds @p account_id afterwards.
 *
 * An UPDATE that matches nothing succeeds in SQL, so an account removed while a
 * credential was being sealed would otherwise report a write that never
 * happened.
 */
sqlite::result<void> updated(const auto& outcome, sqlite::database& db,
    std::string_view account_id) {
    if (auto written = done(outcome); !written)
        return written;

    const auto query = sqlgen::read<std::vector<account_entity>> |
        sqlgen::where("id"_c == std::string{account_id});
    auto rows = rows_of(query(db.connection()));
    if (!rows)
        return std::unexpected(rows.error());
    if (rows.value().empty()) {
        return std::unexpected(sqlite::store_error{
            .code = sqlite::store_errc::not_found,
            .message = std::format("no account '{}' to hold a credential",
                account_id),
            .sqlite_code = 0,
        });
    }
    return {};
}

/** @brief The rows of a prepared account query, as domain values. */
sqlite::result<std::vector<dom::account>> collect(const auto& query,
    sqlite::database& db) {
    return rows_of(query(db.connection())).transform([](const auto& rows) {
        std::vector<dom::account> values;
        values.reserve(rows.size());
        for (const auto& row : rows)
            values.push_back(to_domain(row));
        return values;
    });
}

}

account_repository::account_repository(sqlite::database& db) noexcept
    : db_(&db) {}

std::string account_repository::sql() const {
    return ddl_for(account_entity::tablename);
}

sqlite::result<std::vector<dom::account>> account_repository::list() const {
    const auto query = sqlgen::read<std::vector<account_entity>> |
        sqlgen::order_by("instance_id"_c, "name"_c);
    return collect(query, *db_);
}

sqlite::result<std::vector<dom::account>>
account_repository::list_for_instance(std::string_view instance_id) const {
    const auto query = sqlgen::read<std::vector<account_entity>> |
        sqlgen::where("instance_id"_c == std::string{instance_id}) |
        sqlgen::order_by("name"_c);
    return collect(query, *db_);
}

sqlite::result<void> account_repository::save(const dom::account& row) {
    return upsert(*db_, to_entity(row), {"id"});
}

sqlite::result<void> account_repository::set_parent(std::string_view id,
    std::string_view folder_id) {
    const auto query =
        sqlgen::update<account_entity>("folder_id"_c.set(std::string{folder_id})) |
        sqlgen::where("id"_c == std::string{id});
    return done(query(db_->connection()));
}

sqlite::result<void> account_repository::set_party(std::string_view id,
    std::string_view party_id) {
    /*
     * An UPDATE rather than an upsert, for the credential's reason: the party
     * belongs to an account that exists, and a typo in an id must not conjure a
     * row with no instance, no name and no principal.
     */
    const auto query =
        sqlgen::update<account_entity>("party_id"_c.set(std::string{party_id})) |
        sqlgen::where("id"_c == std::string{id});
    return updated(query(db_->connection()), *db_, id);
}

sqlite::result<void> account_repository::remove(std::string_view id) {
    const auto query = sqlgen::delete_from<account_entity> |
        sqlgen::where("id"_c == std::string{id});
    return done(query(db_->connection()));
}

sqlite::result<void> account_repository::remove_for_instance(
    std::string_view instance_id) {
    const auto query = sqlgen::delete_from<account_entity> |
        sqlgen::where("instance_id"_c == std::string{instance_id});
    return done(query(db_->connection()));
}

sqlite::result<void> account_repository::set_credential(
    std::string_view account_id, std::string_view sealed) {
    /*
     * An UPDATE rather than an upsert: a credential belongs to an account that
     * exists, and an upsert here would conjure a row with no instance, no name
     * and no principal for a typo in an id.
     */
    const auto query =
        sqlgen::update<account_credential_entity>(
            "credential"_c.set(std::string{sealed})) |
        sqlgen::where("id"_c == std::string{account_id});
    return updated(query(db_->connection()), *db_, account_id);
}

sqlite::result<void>
account_repository::clear_credential(std::string_view account_id) {
    /*
     * Null rather than the empty string, because the two say different things:
     * a NULL column is an account nobody has stored a secret for, while an
     * empty blob would be a secret whose seal produced nothing.
     */
    const auto query =
        sqlgen::update<account_credential_entity>(
            "credential"_c.set(std::optional<std::string>{})) |
        sqlgen::where("id"_c == std::string{account_id});
    return updated(query(db_->connection()), *db_, account_id);
}

sqlite::result<std::vector<std::string>>
account_repository::ids_with_credential() const {
    /*
     * The column is selected and the rows are filtered here rather than in SQL,
     * because sqlgen's null comparison is not part of the vocabulary these
     * repositories use. Only the id is kept, so the sealed values are read by
     * the driver and dropped with the row that carried them.
     */
    const auto query =
        sqlgen::read<std::vector<account_credential_entity>>;
    auto rows = rows_of(query(db_->connection()));
    if (!rows)
        return std::unexpected(rows.error());

    std::vector<std::string> ids;
    for (const auto& row : rows.value()) {
        if (row.credential)
            ids.push_back(row.id.get());
    }
    return ids;
}

sqlite::result<std::optional<std::string>>
account_repository::credential_of(std::string_view account_id) const {
    const auto query = sqlgen::read<std::vector<account_credential_entity>> |
        sqlgen::where("id"_c == std::string{account_id});

    auto rows = rows_of(query(db_->connection()));
    if (!rows)
        return std::unexpected(rows.error());
    if (rows.value().empty())
        return std::optional<std::string>{};
    return rows.value().front().credential;
}

}
