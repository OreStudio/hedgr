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
#include "hedgr.store/sqlite/statement.hpp"
#include <format>
#include <sqlite3.h>
#include <string>
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

statement::statement(sqlite3* db, std::string_view sql) : db_(db) {
    if (db == nullptr) {
        prepared_ = std::unexpected(store_error{
            .code = store_errc::closed,
            .message = std::format("cannot prepare [{}] on a closed database",
                                   sql),
            .sqlite_code = 0,
        });
        return;
    }

    const std::string owned(sql);
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db, owned.c_str(),
                                      static_cast<int>(owned.size()) + 1, &raw,
                                      nullptr);
    if (rc != SQLITE_OK) {
        prepared_ = std::unexpected(
            make_error(store_errc::prepare_failed, db, rc,
                       std::format("could not prepare [{}]", sql)));
        return;
    }
    // A statement that compiles to nothing is a comment or empty text; keeping
    // it would turn every later step into a silent no-op.
    if (raw == nullptr) {
        prepared_ = std::unexpected(store_error{
            .code = store_errc::prepare_failed,
            .message = std::format("could not prepare [{}]: no statement", sql),
            .sqlite_code = rc,
        });
        return;
    }
    handle_ = raw;
}

statement::~statement() {
    if (handle_ != nullptr)
        sqlite3_finalize(handle_);
}

statement::statement(statement&& other) noexcept
    : prepared_(std::move(other.prepared_)), db_(other.db_),
      handle_(std::exchange(other.handle_, nullptr)) {}

statement& statement::operator=(statement&& other) noexcept {
    if (this == &other)
        return *this;
    if (handle_ != nullptr)
        sqlite3_finalize(handle_);
    prepared_ = std::move(other.prepared_);
    db_ = other.db_;
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
}

bool statement::is_valid() const noexcept {
    return handle_ != nullptr && prepared_.has_value();
}

result<void> statement::bind(int index, std::string_view value) {
    if (!prepared_)
        return std::unexpected(prepared_.error());
    const int rc = sqlite3_bind_text64(handle_, index, value.data(),
                                       static_cast<sqlite3_uint64>(value.size()),
                                       SQLITE_TRANSIENT, SQLITE_UTF8);
    if (rc != SQLITE_OK)
        return std::unexpected(make_error(
            store_errc::bind_failed, db_, rc,
            std::format("could not bind text at index {}", index)));
    return {};
}

result<void> statement::bind(int index, std::int64_t value) {
    if (!prepared_)
        return std::unexpected(prepared_.error());
    const int rc = sqlite3_bind_int64(handle_, index, value);
    if (rc != SQLITE_OK)
        return std::unexpected(make_error(
            store_errc::bind_failed, db_, rc,
            std::format("could not bind integer at index {}", index)));
    return {};
}

result<void> statement::bind_null(int index) {
    if (!prepared_)
        return std::unexpected(prepared_.error());
    const int rc = sqlite3_bind_null(handle_, index);
    if (rc != SQLITE_OK)
        return std::unexpected(make_error(
            store_errc::bind_failed, db_, rc,
            std::format("could not bind NULL at index {}", index)));
    return {};
}

result<bool> statement::step() {
    if (!prepared_)
        return std::unexpected(prepared_.error());

    const int rc = sqlite3_step(handle_);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;

    const store_errc code = (rc & 0xFF) == SQLITE_CONSTRAINT
                                ? store_errc::constraint_violated
                                : store_errc::step_failed;
    return std::unexpected(
        make_error(code, db_, rc, "could not advance the statement"));
}

std::string_view statement::column_text(int column) const {
    if (handle_ == nullptr)
        return {};
    const auto* text =
        reinterpret_cast<const char*>(sqlite3_column_text(handle_, column));
    if (text == nullptr)
        return {};
    const int size = sqlite3_column_bytes(handle_, column);
    return std::string_view(text, static_cast<std::size_t>(size));
}

std::int64_t statement::column_int64(int column) const {
    if (handle_ == nullptr)
        return 0;
    return sqlite3_column_int64(handle_, column);
}

bool statement::is_null(int column) const noexcept {
    if (handle_ == nullptr)
        return true;
    return sqlite3_column_type(handle_, column) == SQLITE_NULL;
}

result<void> statement::reset() {
    if (!prepared_)
        return std::unexpected(prepared_.error());
    const int rc = sqlite3_reset(handle_);
    if (rc != SQLITE_OK)
        return std::unexpected(make_error(
            store_errc::step_failed, db_, rc, "could not reset the statement"));
    // sqlite3_reset leaves the bindings in place, and a stale binding is a
    // silent wrong answer, so they are cleared here rather than left to the
    // caller to remember.
    sqlite3_clear_bindings(handle_);
    return {};
}

}
