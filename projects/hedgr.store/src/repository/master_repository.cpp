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
#include "hedgr.store/sqlite/statement.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::store::repository {

namespace {

namespace crypto = hedgr::store::crypto;

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

}

master_repository::master_repository(sqlite::database& db) noexcept : db_(&db) {}

sqlite::result<bool> master_repository::has_master() const {
    sqlite::statement query(
        db_->handle(), "SELECT COUNT(*) FROM master WHERE id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, master_row_id); !bound)
        return std::unexpected(bound.error());

    auto row = query.step();
    if (!row)
        return std::unexpected(row.error());
    if (!row.value())
        return false;
    return query.column_int64(0) > 0;
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

    sqlite::statement insert(db_->handle(),
        "INSERT INTO master (id, verification) VALUES (?, ?)");
    if (!insert.is_valid())
        return std::unexpected(insert.step().error());

    if (auto bound = insert.bind(1, master_row_id); !bound)
        return std::unexpected(bound.error());
    if (auto bound = insert.bind(2, sealed.value()); !bound)
        return std::unexpected(bound.error());

    auto stepped = insert.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
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
    sqlite::statement query(
        db_->handle(), "SELECT verification FROM master WHERE id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, master_row_id); !bound)
        return std::unexpected(bound.error());

    auto row = query.step();
    if (!row)
        return std::unexpected(row.error());
    if (!row.value())
        return std::string{};
    return std::string(query.column_text(0));
}

sqlite::result<void> master_repository::clear() {
    sqlite::statement query(db_->handle(), "DELETE FROM master WHERE id = ?");
    if (!query.is_valid())
        return std::unexpected(query.step().error());

    if (auto bound = query.bind(1, master_row_id); !bound)
        return std::unexpected(bound.error());

    auto stepped = query.step();
    if (!stepped)
        return std::unexpected(stepped.error());
    return {};
}

}
