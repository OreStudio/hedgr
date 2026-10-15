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
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.model/domain/window_json_io.hpp"
#include "hedgr.store/sqlite/statement.hpp"
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

constexpr std::string_view select_columns =
    "SELECT window_id, kind, session_instance_id, session_account_id, "
    "cadence_ms, state_json FROM window_spec ORDER BY window_id";

sqlite::store_error corrupt_row(std::string_view window_id,
                                std::string_view detail) {
    return sqlite::store_error{
        .code = sqlite::store_errc::corrupt_row,
        .message = std::format("window {} is malformed: {}", window_id, detail),
        .sqlite_code = 0,
    };
}

sqlite::result<std::string> encode_state(const dom::window_spec& spec) {
    auto encoded = dom::window_state_to_json(spec.kind, spec.state);
    if (!encoded)
        return std::unexpected(
            corrupt_row(spec.id, std::format("cannot serialise its state: {}",
                                             encoded.error())));
    return std::move(encoded).value();
}

sqlite::result<dom::window_spec> read_row(const sqlite::statement& query) {
    dom::window_spec spec;
    spec.id = std::string(query.column_text(0));

    const auto kind = dom::window_kind_from_string(query.column_text(1));
    if (!kind)
        return std::unexpected(corrupt_row(
            spec.id, std::format("unknown kind [{}]: {}", query.column_text(1),
                                 kind.error())));
    spec.kind = kind.value();

    // An absent session is stored as NULL on both columns, so the optional is
    // rebuilt from the instance column alone and cannot be half-present.
    if (!query.is_null(2)) {
        spec.session = dom::session_key{
            std::string(query.column_text(2)),
            std::string(query.column_text(3)),
        };
    }

    spec.cadence =
        std::chrono::milliseconds(query.column_int64(4));

    auto state = dom::window_state_from_json(spec.kind, query.column_text(5));
    if (!state)
        return std::unexpected(corrupt_row(
            spec.id, std::format("cannot parse its state: {}", state.error())));
    spec.state = std::move(state).value();
    return spec;
}

}

window_repository::window_repository(sqlite::database& db) noexcept
    : db_(&db) {}

sqlite::result<std::vector<dom::window_spec>> window_repository::list() const {
    sqlite::statement query(db_->handle(), select_columns);
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    std::vector<dom::window_spec> rows;
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

sqlite::result<void> window_repository::save(const dom::window_spec& spec) {
    auto encoded = encode_state(spec);
    if (!encoded)
        return std::unexpected(encoded.error());

    sqlite::statement insert(
        db_->handle(),
        "INSERT INTO window_spec (window_id, kind, session_instance_id, "
        "session_account_id, cadence_ms, state_json) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(window_id) DO UPDATE SET kind = excluded.kind, "
        "session_instance_id = excluded.session_instance_id, "
        "session_account_id = excluded.session_account_id, "
        "cadence_ms = excluded.cadence_ms, "
        "state_json = excluded.state_json");
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    if (auto bound = insert.bind(1, spec.id); !bound)
        return bound;
    if (auto bound = insert.bind(2, dom::to_string(spec.kind)); !bound)
        return bound;
    if (spec.session) {
        if (auto bound = insert.bind(3, spec.session->instance_id); !bound)
            return bound;
        if (auto bound = insert.bind(4, spec.session->account_id); !bound)
            return bound;
    } else {
        if (auto bound = insert.bind_null(3); !bound)
            return bound;
        if (auto bound = insert.bind_null(4); !bound)
            return bound;
    }
    if (auto bound = insert.bind(
            5, static_cast<std::int64_t>(spec.cadence.count()));
        !bound)
        return bound;
    if (auto bound = insert.bind(6, encoded.value()); !bound)
        return bound;

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

sqlite::result<void> window_repository::remove(std::string_view window_id) {
    sqlite::statement query(db_->handle(),
                            "DELETE FROM window_spec WHERE window_id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, window_id); !bound)
        return bound;

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

}
