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
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
#include <chrono>
#include <cstdint>
#include <format>
#include <sqlgen/delete_from.hpp>
#include <sqlgen/literals.hpp>
#include <sqlgen/order_by.hpp>
#include <sqlgen/read.hpp>
#include <sqlgen/where.hpp>
#include <string>
#include <utility>
#include <vector>

namespace hedgr::store::repository {

// The repositories are the boundary between stored text and domain values, so
// the domain namespace is spelled once here rather than in every row.
namespace dom = model::domain;

namespace {

using namespace sqlgen::literals;

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
    if (!encoded) {
        return std::unexpected(
            corrupt_row(spec.id, std::format("cannot serialise its state: {}",
                                             encoded.error())));
    }
    return std::move(encoded).value();
}

/** @brief The domain value as the entity sqlgen binds. */
window_spec_entity to_entity(const dom::window_spec& spec,
    std::string state_json) {
    window_spec_entity row;
    row.window_id = spec.id;
    row.kind = std::string{dom::to_string(spec.kind)};
    if (spec.session) {
        row.session_instance_id = spec.session->instance_id;
        row.session_account_id = spec.session->account_id;
    }
    row.cadence_ms = static_cast<std::int64_t>(spec.cadence.count());
    row.state_json = std::move(state_json);
    return row;
}

/** @brief The stored row as a domain value, or why it is not one. */
sqlite::result<dom::window_spec> to_domain(const window_spec_entity& row) {
    dom::window_spec spec;
    spec.id = row.window_id.get();

    const auto kind = dom::window_kind_from_string(row.kind);
    if (!kind) {
        return std::unexpected(corrupt_row(spec.id,
            std::format("unknown kind [{}]: {}", row.kind, kind.error())));
    }
    spec.kind = kind.value();

    // An absent session is stored as NULL on both columns, so the optional is
    // rebuilt from the instance column alone and cannot be half-present.
    if (row.session_instance_id) {
        spec.session = dom::session_key{
            *row.session_instance_id,
            row.session_account_id.value_or(std::string{}),
        };
    }

    spec.cadence = std::chrono::milliseconds(row.cadence_ms);

    auto state = dom::window_state_from_json(spec.kind, row.state_json);
    if (!state) {
        return std::unexpected(corrupt_row(spec.id,
            std::format("cannot parse its state: {}", state.error())));
    }
    spec.state = std::move(state).value();
    return spec;
}

}

window_repository::window_repository(sqlite::database& db) noexcept
    : db_(&db) {}

std::string window_repository::sql() const {
    return ddl_for(window_spec_entity::tablename);
}

sqlite::result<std::vector<dom::window_spec>> window_repository::list() const {
    const auto query = sqlgen::read<std::vector<window_spec_entity>> |
        sqlgen::order_by("window_id"_c);

    auto rows = rows_of(query(db_->connection()));
    if (!rows)
        return std::unexpected(rows.error());

    return valid_rows(rows.value(), window_spec_entity::tablename, to_domain);
}

sqlite::result<void> window_repository::save(const dom::window_spec& spec) {
    auto encoded = encode_state(spec);
    if (!encoded)
        return std::unexpected(encoded.error());

    return upsert(*db_, to_entity(spec, std::move(encoded).value()),
        {"window_id"});
}

sqlite::result<void> window_repository::remove(std::string_view window_id) {
    const auto query = sqlgen::delete_from<window_spec_entity> |
        sqlgen::where("window_id"_c == std::string{window_id});
    return done(query(db_->connection()));
}

}
