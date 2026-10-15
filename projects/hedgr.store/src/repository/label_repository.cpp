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
#include "hedgr.store/repository/label_repository.hpp"
#include "hedgr.store/sqlite/statement.hpp"
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

constexpr std::string_view select_columns =
    "SELECT id, name, colour, severity FROM label ORDER BY name";

sqlite::store_error corrupt_row(std::string_view id, std::string_view detail) {
    return sqlite::store_error{
        .code = sqlite::store_errc::corrupt_row,
        .message = std::format("label {} is malformed: {}", id, detail),
        .sqlite_code = 0,
    };
}

/** @brief Reads the row the statement is currently parked on. */
sqlite::result<dom::label> read_row(const sqlite::statement& query) {
    dom::label row;
    row.id = std::string(query.column_text(0));
    row.name = std::string(query.column_text(1));

    const auto colour = dom::label_colour_from_string(query.column_text(2));
    if (!colour) {
        return std::unexpected(corrupt_row(
            row.id, std::format("unknown colour [{}]: {}", query.column_text(2),
                                colour.error())));
    }
    row.colour = colour.value();

    const auto severity = dom::label_severity_from_string(query.column_text(3));
    if (!severity) {
        return std::unexpected(corrupt_row(
            row.id, std::format("unknown severity [{}]: {}",
                                query.column_text(3), severity.error())));
    }
    row.severity = severity.value();
    return row;
}

}

label_repository::label_repository(sqlite::database& db) noexcept
    : db_(&db) {}

sqlite::result<std::vector<dom::label>> label_repository::list() const {
    sqlite::statement query(db_->handle(), select_columns);
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    std::vector<dom::label> rows;
    while (true) {
        auto available = query.step();
        if (!available)
            return std::unexpected(available.error());
        if (!available.value())
            break;

        auto row = read_row(query);
        if (!row)
            return std::unexpected(row.error());
        rows.push_back(std::move(row).value());
    }
    return rows;
}

sqlite::result<void> label_repository::save(const dom::label& row) {
    sqlite::statement insert(
        db_->handle(),
        "INSERT INTO label (id, name, colour, severity) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, "
        "colour = excluded.colour, severity = excluded.severity");
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    if (auto bound = insert.bind(1, row.id); !bound)
        return bound;
    if (auto bound = insert.bind(2, row.name); !bound)
        return bound;
    if (auto bound = insert.bind(3, dom::to_string(row.colour)); !bound)
        return bound;
    if (auto bound = insert.bind(4, dom::to_string(row.severity)); !bound)
        return bound;

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> label_repository::remove(std::string_view id) {
    sqlite::statement query(db_->handle(), "DELETE FROM label WHERE id = ?");
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
