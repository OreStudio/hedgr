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
#include "hedgr.store/sqlite/database.hpp"
#include "hedgr.store/repository/schema.hpp"
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace hedgr::store::sqlite {

store_error from_sqlgen(const rfl::Error& error, store_errc fallback) {
    const std::string text = error.what();
    /*
     * A refused write is the one failure a caller is expected to act on, and
     * SQLite names it in the message. Everything else keeps the code the
     * operation asked for, which is why the fallback is a parameter: a failed
     * compile and a failed step are the same kind of sentence to sqlgen.
     */
    const bool constraint = text.find(" constraint failed") != std::string::npos;

    return store_error{
        .code = constraint ? store_errc::constraint_violated : fallback,
        .message = text,
        .sqlite_code = 0,
    };
}

namespace {

result<void> pragma(const sqlgen::Ref<sqlgen::sqlite::Connection>& handle,
    std::string_view sql) {
    auto applied = handle->execute(std::string(sql));
    if (!applied) {
        return std::unexpected(
            from_sqlgen(applied.error(), store_errc::open_failed));
    }
    return {};
}

}

database::~database() = default;

database::database(sqlgen::Ref<sqlgen::sqlite::Connection> raw,
    std::filesystem::path path) noexcept
    : handle_(std::move(raw)), path_(std::move(path)) {}

database::database(database&& other) noexcept
    : handle_(std::move(other.handle_)), path_(std::move(other.path_)) {}

database& database::operator=(database&& other) noexcept {
    if (this == &other)
        return *this;
    handle_ = std::move(other.handle_);
    path_ = std::move(other.path_);
    return *this;
}

result<database> database::adopt(
    const sqlgen::Ref<sqlgen::sqlite::Connection>& raw,
    std::filesystem::path path) {
    /*
     * Foreign keys are per-connection in SQLite and default to off, so a
     * repository that relies on ON DELETE CASCADE must see this run first.
     * WAL keeps a client that is killed mid-write from leaving a store that has
     * to be recovered before it can be read.
     */
    for (const auto& sql : {std::string_view("PRAGMA foreign_keys = ON"),
                            std::string_view("PRAGMA journal_mode = WAL")}) {
        if (auto applied = pragma(raw, sql); !applied)
            return std::unexpected(applied.error());
    }
    return database(raw, std::move(path));
}

result<database> database::open(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code failure;
        std::filesystem::create_directories(parent, failure);
        if (failure)
            return std::unexpected(store_error{
                .code = store_errc::open_failed,
                .message = std::format("could not create {}: {}",
                                       parent.string(), failure.message()),
                .sqlite_code = 0,
            });
    }

    auto opened = sqlgen::sqlite::connect(path.string());
    if (!opened) {
        return std::unexpected(
            from_sqlgen(opened.error(), store_errc::open_failed));
    }
    return adopt(opened.value(), path);
}

result<database> database::open_in_memory() {
    auto opened = sqlgen::sqlite::connect(":memory:");
    if (!opened) {
        return std::unexpected(
            from_sqlgen(opened.error(), store_errc::open_failed));
    }
    return adopt(opened.value(), {});
}

result<void> database::execute(std::string_view sql) {
    if (!is_open())
        return std::unexpected(store_error{
            .code = store_errc::closed,
            .message = "cannot execute on a closed database",
            .sqlite_code = 0,
        });

    auto applied = handle_->execute(std::string(sql));
    if (!applied) {
        return std::unexpected(
            from_sqlgen(applied.error(), store_errc::step_failed));
    }
    return {};
}

result<void> database::apply_schema() {
    if (!is_open())
        return std::unexpected(store_error{
            .code = store_errc::closed,
            .message = "cannot apply the schema to a closed database",
            .sqlite_code = 0,
        });
    return repository::apply(*this);
}

bool database::is_open() const noexcept { return handle_.ptr() != nullptr; }

const sqlgen::Ref<sqlgen::sqlite::Connection>&
database::connection() const noexcept {
    return handle_;
}

std::filesystem::path database::path() const { return path_; }

}
