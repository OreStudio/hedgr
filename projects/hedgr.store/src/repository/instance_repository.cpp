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
#include "hedgr.store/sqlite/statement.hpp"
#include <string>
#include <string_view>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

constexpr std::string_view select_columns =
    "SELECT id, name, nats_url, subject_prefix, wire_format, created_at "
    "FROM instance ORDER BY name";

/** @brief Reads the row the statement is currently parked on. */
dom::instance read_row(const sqlite::statement& query) {
    dom::instance row;
    row.id = std::string(query.column_text(0));
    row.name = std::string(query.column_text(1));
    row.nats_url = std::string(query.column_text(2));
    row.subject_prefix = std::string(query.column_text(3));
    row.wire_format = std::string(query.column_text(4));
    row.created_at = std::string(query.column_text(5));
    return row;
}

}

instance_repository::instance_repository(sqlite::database& db) noexcept
    : db_(&db) {}

sqlite::result<std::vector<dom::instance>> instance_repository::list() const {
    sqlite::statement query(db_->handle(), select_columns);
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    std::vector<dom::instance> rows;
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

sqlite::result<void> instance_repository::save(const dom::instance& row) {
    sqlite::statement insert(
        db_->handle(),
        "INSERT INTO instance (id, name, nats_url, subject_prefix, "
        "wire_format) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, "
        "nats_url = excluded.nats_url, "
        "subject_prefix = excluded.subject_prefix, "
        "wire_format = excluded.wire_format");
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    // created_at is deliberately not written: the stored timestamp records when
    // the operator first added the instance, which an edit must not move.
    if (auto bound = insert.bind(1, row.id); !bound)
        return bound;
    if (auto bound = insert.bind(2, row.name); !bound)
        return bound;
    if (auto bound = insert.bind(3, row.nats_url); !bound)
        return bound;
    if (auto bound = insert.bind(4, row.subject_prefix); !bound)
        return bound;
    if (auto bound = insert.bind(5, row.wire_format); !bound)
        return bound;

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> instance_repository::remove(std::string_view id) {
    sqlite::statement query(db_->handle(), "DELETE FROM instance WHERE id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

}
