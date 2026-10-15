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
#include <sqlite3.h>
#include <system_error>
#include <utility>

namespace hedgr::store::sqlite {

namespace {

store_error make_error(store_errc code, sqlite3* db, int sqlite_code,
                       std::string_view detail) {
    const char* text = db == nullptr ? nullptr : sqlite3_errmsg(db);
    return store_error{
        .code = code,
        .message = std::format("{}: {}", detail,
                               text == nullptr ? "no error text" : text),
        .sqlite_code = sqlite_code,
    };
}

}

database::~database() {
    if (handle_ != nullptr)
        sqlite3_close(handle_);
}

database::database(sqlite3* raw) noexcept : handle_(raw) {}

database::database(database&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

database& database::operator=(database&& other) noexcept {
    if (this == &other)
        return *this;
    if (handle_ != nullptr)
        sqlite3_close(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
}

result<database> database::adopt(sqlite3* raw) {
    // Foreign keys are per-connection in SQLite and default to off, so a
    // repository that relies on ON DELETE CASCADE must see this run first.
    auto pragma = [raw](std::string_view sql) -> result<void> {
        char* text = nullptr;
        const std::string owned(sql);
        const int rc =
            sqlite3_exec(raw, owned.c_str(), nullptr, nullptr, &text);
        if (rc == SQLITE_OK)
            return {};
        const std::string message =
            text == nullptr ? "no error text" : std::string(text);
        sqlite3_free(text);
        return std::unexpected(store_error{
            .code = store_errc::open_failed,
            .message = std::format("could not set {}: {}", sql, message),
            .sqlite_code = rc,
        });
    };

    for (const auto& sql : {std::string_view("PRAGMA foreign_keys = ON"),
                            std::string_view("PRAGMA journal_mode = WAL")}) {
        if (auto applied = pragma(sql); !applied) {
            // A connection that does not have the pragmas we require is worse
            // than no connection, and the raw handle would otherwise leak.
            store_error error = applied.error();
            sqlite3_close(raw);
            return std::unexpected(std::move(error));
        }
    }
    return database(raw);
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

    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(
        path.string().c_str(), &raw,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        // sqlite3_open_v2 hands back a handle even on failure, and only that
        // handle knows why the open failed.
        store_error error = make_error(store_errc::open_failed, raw, rc,
                                       std::format("could not open {}",
                                                   path.string()));
        if (raw != nullptr)
            sqlite3_close_v2(raw);
        return std::unexpected(std::move(error));
    }
    return adopt(raw);
}

result<database> database::open_in_memory() {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(":memory:", &raw,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   nullptr);
    if (rc != SQLITE_OK) {
        store_error error = make_error(store_errc::open_failed, raw, rc,
                                       "could not open an in-memory database");
        if (raw != nullptr)
            sqlite3_close_v2(raw);
        return std::unexpected(std::move(error));
    }
    return adopt(raw);
}

result<void> database::execute(std::string_view sql) {
    if (handle_ == nullptr)
        return std::unexpected(store_error{
            .code = store_errc::closed,
            .message = "cannot execute on a closed database",
            .sqlite_code = 0,
        });

    char* text = nullptr;
    const int rc = sqlite3_exec(handle_, std::string(sql).c_str(), nullptr,
                               nullptr, &text);
    if (rc == SQLITE_OK)
        return {};
    const std::string detail = text == nullptr ? "no error text" : text;
    sqlite3_free(text);
    return std::unexpected(store_error{
        .code = rc == SQLITE_CONSTRAINT ? store_errc::constraint_violated
                                        : store_errc::step_failed,
        .message = std::format("could not execute [{}]: {}", sql, detail),
        .sqlite_code = rc,
    });
}

result<void> database::apply_schema() {
    if (handle_ == nullptr)
        return std::unexpected(store_error{
            .code = store_errc::closed,
            .message = "cannot apply the schema to a closed database",
            .sqlite_code = 0,
        });
    return repository::apply(*this);
}

bool database::is_open() const noexcept { return handle_ != nullptr; }

sqlite3* database::handle() const noexcept { return handle_; }

std::filesystem::path database::path() const {
    if (handle_ == nullptr)
        return {};
    const char* name = sqlite3_db_filename(handle_, "main");
    if (name == nullptr)
        return {};
    return std::filesystem::path(name);
}

}
