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
#include "hedgr.store/repository/master_repository.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/query.hpp"
#include <sqlgen/delete_from.hpp>
#include <sqlgen/literals.hpp>
#include <sqlgen/read.hpp>
#include <sqlgen/where.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::store::repository {

namespace {

namespace crypto = hedgr::store::crypto;

using namespace sqlgen::literals;

/**
 * @brief The row's primary key: there is exactly one master password.
 *
 * A literal rather than a generated id, so a second @c create collides with
 * the first instead of adding a second verification blob whose password would
 * silently disagree with the one that opens the credentials.
 */
constexpr std::string_view master_row_id = "master";

/**
 * @brief What gets sealed to prove the password.
 *
 * A fixed sentence, and deliberately not a random value. It has to be known in
 * advance: verification works by opening the blob and comparing the plaintext
 * with this constant, so a random one would have to be stored beside the blob
 * and would prove nothing. It is not a secret, because the seal is.
 *
 * The text names its own purpose so that an operator who finds it in a hex dump
 * of the file knows what they are looking at.
 */
constexpr std::string_view verification_plaintext =
    "hedgr.master.verification";

/** @brief A crypto failure as a store failure, keeping the code. */
sqlite::store_error to_store_error(const crypto::box_error& error) {
    return sqlite::store_error{
        .code = error.code == crypto::box_errc::wrong_master_password
            ? sqlite::store_errc::wrong_master_password
            : sqlite::store_errc::crypto_failed,
        .message = error.message,
        .sqlite_code = 0,
    };
}

/** @brief The one row, when it is there. */
sqlite::result<std::optional<master_entity>> the_row(sqlite::database& db) {
    const auto query = sqlgen::read<std::vector<master_entity>> |
        sqlgen::where("id"_c == std::string{master_row_id});

    auto rows = rows_of(query(db.connection()));
    if (!rows)
        return std::unexpected(rows.error());
    if (rows.value().empty())
        return std::optional<master_entity>{};
    return std::optional<master_entity>{rows.value().front()};
}

}

master_repository::master_repository(sqlite::database& db) noexcept : db_(&db) {}

std::string master_repository::sql() const {
    return ddl_for(master_entity::tablename);
}

sqlite::result<bool> master_repository::has_master() const {
    auto row = the_row(*db_);
    if (!row)
        return std::unexpected(row.error());
    return row.value().has_value();
}

sqlite::result<void> master_repository::create(std::string_view password) {
    /*
     * Refused rather than replaced. Every stored credential was sealed under
     * whichever password is in this row, so quietly overwriting it would leave
     * a store full of blobs nobody can open while looking, from the outside,
     * exactly like a store that works.
     */
    auto existing = has_master();
    if (!existing)
        return std::unexpected(existing.error());
    if (existing.value()) {
        return std::unexpected(sqlite::store_error{
            .code = sqlite::store_errc::constraint_violated,
            .message = "this store already has a master password; clear it "
                       "deliberately before creating another",
            .sqlite_code = 0,
        });
    }

    auto sealed = crypto::secret_box::seal(password, verification_plaintext);
    if (!sealed)
        return std::unexpected(to_store_error(sealed.error()));

    /*
     * The creation stamp used to come from a DEFAULT on the column, which
     * sqlgen cannot express. The writer stamps it instead, which is also the
     * only place that knows the row is being created rather than replaced.
     */
    return upsert(*db_,
        master_entity{
            .id = std::string{master_row_id},
            .verification = std::move(sealed).value(),
            .created_at = timestamp_now(),
        },
        {"id"});
}

sqlite::result<bool> master_repository::verify(std::string_view password) const {
    auto blob = verification();
    if (!blob)
        return std::unexpected(blob.error());

    /*
     * No row is not "the password is wrong": it is a question that cannot be
     * asked. Answering false would let a caller report a refused password for a
     * store that never had one, which is the state the interface calls absent.
     */
    if (blob.value().empty()) {
        return std::unexpected(sqlite::store_error{
            .code = sqlite::store_errc::not_found,
            .message = "this store has no master password to verify",
            .sqlite_code = 0,
        });
    }

    auto opened = crypto::secret_box::open(password, blob.value());
    if (!opened) {
        /*
         * A wrong password is the answer false. A crypto failure is not an
         * answer at all, so it stays an error and keeps its own code rather
         * than being reported as a rejection.
         */
        if (opened.error().code == crypto::box_errc::wrong_master_password)
            return false;
        return std::unexpected(to_store_error(opened.error()));
    }

    return opened.value() == verification_plaintext;
}

sqlite::result<std::string> master_repository::verification() const {
    auto row = the_row(*db_);
    if (!row)
        return std::unexpected(row.error());
    if (!row.value())
        return std::string{};
    return row.value()->verification;
}

sqlite::result<void> master_repository::clear() {
    const auto query = sqlgen::delete_from<master_entity> |
        sqlgen::where("id"_c == std::string{master_row_id});
    return done(query(db_->connection()));
}

}
