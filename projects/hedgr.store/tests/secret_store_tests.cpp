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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/master_repository.hpp"
#include "hedgr.store/repository/schema.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sqlgen/read.hpp>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

using hedgr::model::domain::account;
using hedgr::model::domain::instance;
using hedgr::store::crypto::box_errc;
using hedgr::store::crypto::secret_box;
using hedgr::store::repository::account_repository;
using hedgr::store::repository::instance_repository;
using hedgr::store::repository::master_repository;
using hedgr::store::sqlite::database;
using hedgr::store::sqlite::store_errc;

/** @brief An open in-memory store with the schema already applied. */
database fresh_store() {
    auto opened = database::open_in_memory();
    REQUIRE(opened.has_value());
    database db = std::move(opened).value();
    REQUIRE(db.apply_schema().has_value());
    return db;
}

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * The tests below that need a file are the ones that assert about a file: the
 * version it carries and, above all, whether a password appears among its
 * bytes. Both are meaningless against a private in-memory database. The name
 * carries a per-run token and a serial, so a file left behind by a killed run
 * can never be picked up by the next one.
 */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.store.secrets.{}.{}.sqlite", unique_token(),
                  next_serial())) {
        erase();
    }

    ~temporary_database() {
        erase();
    }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    void erase() const {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    std::filesystem::path path_;
};

/** @brief Every byte of a file, for a search that must not miss anything. */
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    return std::string{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

/** @brief An instance row, so that an account has somewhere to belong. */
instance sample_instance() {
    instance row;
    row.id = "inst-1";
    row.name = "Local";
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = "ores.local";
    row.wire_format = "msgpack";
    return row;
}

account sample_account() {
    account row;
    row.id = "acct-1";
    row.instance_id = "inst-1";
    row.name = "ops read-only";
    row.principal = "ops@host";
    return row;
}

}

TEST_CASE("a sealed value opens back to its plaintext", "[secret_box]") {
    const auto sealed = secret_box::seal("correct horse battery staple", "hunter2");
    REQUIRE(sealed.has_value());
    REQUIRE_FALSE(sealed.value().empty());

    const auto opened = secret_box::open("correct horse battery staple", sealed.value());
    REQUIRE(opened.has_value());
    REQUIRE(opened.value() == "hunter2");
}

TEST_CASE("the same plaintext seals differently every time", "[secret_box]") {
    /*
     * A fresh salt and a fresh nonce per seal, which is what stops the store
     * revealing that two accounts share a password. It is also the property a
     * nonce must have: reusing one under the same key breaks GCM outright.
     */
    const auto first = secret_box::seal("a-password", "the same plaintext");
    const auto second = secret_box::seal("a-password", "the same plaintext");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() != second.value());
}

TEST_CASE("a wrong master password is reported as a wrong password, not as "
          "corruption",
    "[secret_box]") {
    const auto sealed = secret_box::seal("the-right-password", "hunter2");
    REQUIRE(sealed.has_value());

    const auto opened = secret_box::open("the-wrong-password", sealed.value());
    REQUIRE_FALSE(opened.has_value());

    /*
     * The code is the whole point of this test. An operator can act on a wrong
     * password by typing a different one, and cannot act on a corrupted store
     * at all, so the two must not arrive as the same thing under a name that
     * suggests either.
     */
    REQUIRE(opened.error().code == box_errc::wrong_master_password);
    REQUIRE(hedgr::store::crypto::to_string(opened.error().code)
        == "wrong_master_password");
}

TEST_CASE("a tampered ciphertext is refused", "[secret_box]") {
    const auto sealed = secret_box::seal("the-right-password", "hunter2");
    REQUIRE(sealed.has_value());

    /*
     * One character of the base64 is changed, in the middle of the value where
     * it lands in the ciphertext rather than in the salt or the nonce. A
     * single-character change is the smallest tamper there is, and GCM's tag
     * has to catch it.
     */
    std::string tampered = sealed.value();
    const auto middle = tampered.size() / 2;
    tampered[middle] = tampered[middle] == 'A' ? 'B' : 'A';

    const auto opened = secret_box::open("the-right-password", tampered);
    REQUIRE_FALSE(opened.has_value());
    REQUIRE(opened.error().code == box_errc::wrong_master_password);
}

TEST_CASE("a truncated sealed value is refused", "[secret_box]") {
    const auto sealed = secret_box::seal("the-right-password", "hunter2");
    REQUIRE(sealed.has_value());

    /* Shorter than the salt, nonce and tag together: there is nothing to open. */
    const auto opened =
        secret_box::open("the-right-password", sealed.value().substr(0, 20));
    REQUIRE_FALSE(opened.has_value());
    REQUIRE(opened.error().code == box_errc::wrong_master_password);
}

TEST_CASE("an empty sealed value is refused", "[secret_box]") {
    const auto opened = secret_box::open("the-right-password", "");
    REQUIRE_FALSE(opened.has_value());
    REQUIRE(opened.error().code == box_errc::wrong_master_password);
}

TEST_CASE("sealing under an empty master password is refused", "[secret_box]") {
    /*
     * A file that looks protected and is not is worse than no protection, and
     * the create prompt exists precisely so that an operator is never one empty
     * field away from it.
     */
    const auto sealed = secret_box::seal("", "hunter2");
    REQUIRE_FALSE(sealed.has_value());
    REQUIRE(sealed.error().code == box_errc::crypto_failed);
}

TEST_CASE("a sealed empty string opens back to an empty string", "[secret_box]") {
    const auto sealed = secret_box::seal("a-password", "");
    REQUIRE(sealed.has_value());

    const auto opened = secret_box::open("a-password", sealed.value());
    REQUIRE(opened.has_value());
    REQUIRE(opened.value().empty());
}

TEST_CASE("creating a master password then verifying it succeeds", "[master]") {
    auto db = fresh_store();
    master_repository masters(db);

    REQUIRE_FALSE(masters.has_master().value());

    REQUIRE(masters.create("correct horse battery staple").has_value());
    REQUIRE(masters.has_master().value());
    REQUIRE(masters.verify("correct horse battery staple").value());
}

TEST_CASE("a wrong master password verifies as false and not as an error",
    "[master]") {
    auto db = fresh_store();
    master_repository masters(db);
    REQUIRE(masters.create("the-real-password").has_value());

    const auto verified = masters.verify("not-the-password");
    REQUIRE(verified.has_value());
    REQUIRE(verified.value() == false);
}

TEST_CASE("a store refuses a second master password", "[master]") {
    auto db = fresh_store();
    master_repository masters(db);
    REQUIRE(masters.create("the-first-password").has_value());

    /*
     * Refused rather than replaced: every stored credential is sealed under the
     * password already in the row, so overwriting it would leave a store that
     * looks healthy and opens nothing.
     */
    const auto again = masters.create("the-second-password");
    REQUIRE_FALSE(again.has_value());
    REQUIRE(again.error().code == store_errc::constraint_violated);
    REQUIRE(masters.verify("the-first-password").value());
}

TEST_CASE("verifying against a store with no master password is an error",
    "[master]") {
    auto db = fresh_store();
    master_repository masters(db);

    const auto verified = masters.verify("anything");
    REQUIRE_FALSE(verified.has_value());
    REQUIRE(verified.error().code == store_errc::not_found);
}

TEST_CASE("clearing the master password removes it", "[master]") {
    auto db = fresh_store();
    master_repository masters(db);
    REQUIRE(masters.create("a-password").has_value());

    REQUIRE(masters.clear().has_value());
    REQUIRE_FALSE(masters.has_master().value());
}

TEST_CASE("an account credential round-trips and can be cleared", "[credential]") {
    auto db = fresh_store();
    instance_repository instances(db);
    account_repository accounts(db);
    REQUIRE(instances.save(sample_instance()).has_value());
    REQUIRE(accounts.save(sample_account()).has_value());

    REQUIRE(accounts.credential_of("acct-1").value() == std::nullopt);

    const auto sealed = secret_box::seal("a-master-password", "hunter2");
    REQUIRE(sealed.has_value());
    REQUIRE(accounts.set_credential("acct-1", sealed.value()).has_value());

    const auto stored = accounts.credential_of("acct-1");
    REQUIRE(stored.has_value());
    REQUIRE(stored.value().has_value());
    REQUIRE(stored.value().value() == sealed.value());

    /* What comes back is still ciphertext, and it opens with the password. */
    const auto opened = secret_box::open("a-master-password", stored.value().value());
    REQUIRE(opened.has_value());
    REQUIRE(opened.value() == "hunter2");

    REQUIRE(accounts.clear_credential("acct-1").has_value());
    REQUIRE(accounts.credential_of("acct-1").value() == std::nullopt);
}

TEST_CASE("a list of accounts carries no credential", "[credential]") {
    auto db = fresh_store();
    instance_repository instances(db);
    account_repository accounts(db);
    REQUIRE(instances.save(sample_instance()).has_value());
    REQUIRE(accounts.save(sample_account()).has_value());

    const auto sealed = secret_box::seal("a-master-password", "hunter2");
    REQUIRE(sealed.has_value());
    REQUIRE(accounts.set_credential("acct-1", sealed.value()).has_value());

    /*
     * The test that keeps a rendering path from becoming a secret path. A list
     * is for drawing, so it must not carry what a list has no use for: a list
     * that came back with the credential attached would put every secret in the
     * store on the path a table draws from, one forgotten reference away from a
     * window, a diff or a log.
     *
     * The property is structural and pinned that way: a listed row is an
     * @c account, and an @c account has no member that could hold a credential.
     * A shared field someone later adds by hand is not what this checks; what
     * it checks is that the type the list yields cannot carry one, so the only
     * way to reach a credential is the call that asks for one account by name.
     */
    const auto rows = accounts.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);

    const auto& row = rows.value().front();
    REQUIRE(row.id == "acct-1");
    REQUIRE(row.principal == "ops@host");
    REQUIRE(row.name == "ops read-only");
    REQUIRE(row.instance_id == "inst-1");

    STATIC_REQUIRE(std::is_same_v<std::decay_t<decltype(rows.value())>,
        std::vector<hedgr::model::domain::account>>);

    /*
     * And the one call that does reach a credential still returns it sealed, so
     * even the caller that asks for a secret has to have the master password to
     * do anything with it.
     */
    const auto stored = accounts.credential_of("acct-1");
    REQUIRE(stored.has_value());
    REQUIRE(stored.value().has_value());
    REQUIRE_FALSE(secret_box::open("a-master-password", stored.value().value())
                      .value()
                      .empty());
}

TEST_CASE("the schema this build writes is version 6", "[schema]") {
    const temporary_database scratch;
    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened.value().apply_schema().has_value());

        const auto rows = opened.value().execute("SELECT 1");
        REQUIRE(rows.has_value());
    }

    /*
     * Read back through the file rather than through the constant, because a
     * constant that agrees with itself proves nothing about what was written.
     */
    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());

    const auto query = sqlgen::read<
        std::vector<hedgr::store::repository::schema_version_entity>>;
    const auto rows = query(reopened.value().connection());
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);
    REQUIRE(rows.value().front().version
        == hedgr::store::repository::schema_version());
}

TEST_CASE("a file written by an older build is refused", "[schema]") {
    const temporary_database scratch;

    /*
     * A file this build wrote, then lied about: version 3 is the state a store
     * from an earlier build is in, and the columns it would have are the ones
     * this build no longer expects.
     */
    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened.value().apply_schema().has_value());
        REQUIRE(opened.value().execute("UPDATE schema_version SET version = 3")
                    .has_value());
    }

    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());

    const auto applied = reopened.value().apply_schema();
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().code == store_errc::schema_mismatch);
    REQUIRE(applied.error().message.contains("3"));
    REQUIRE(applied.error().message.contains(
        std::to_string(hedgr::store::repository::schema_version())));

    /*
     * Refused and never deleted. This unit is what put credentials in that
     * file, so destroying it to save a rebuild would destroy secrets the
     * operator has nowhere else.
     */
    REQUIRE(std::filesystem::exists(scratch.path()));
}

TEST_CASE("the master password appears nowhere in the database file's bytes",
    "[credential]") {
    /*
     * The test that proves the feature's whole point, and the reason it writes
     * a real file rather than using an in-memory database: an assertion about
     * what is on disk has to look at what is on disk.
     *
     * The password is long and contains a run of characters no schema, no id
     * and no sentence in this program contains, so a match could only be the
     * password itself.
     */
    const temporary_database scratch;
    const std::string password =
        "Zq7wXk2mPv9LrTb4Nc6Yd8Fh1Js3Gv5Bw";

    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened.value().apply_schema().has_value());
        database db = std::move(opened).value();

        instance_repository instances(db);
        account_repository accounts(db);
        master_repository masters(db);

        REQUIRE(instances.save(sample_instance()).has_value());
        REQUIRE(accounts.save(sample_account()).has_value());
        REQUIRE(masters.create(password).has_value());

        const auto sealed = secret_box::seal(password, "the-secret-credential");
        REQUIRE(sealed.has_value());
        REQUIRE(accounts.set_credential("acct-1", sealed.value()).has_value());
    }

    /*
     * The connection is closed and everything it buffered is on disk, because
     * the database object above went out of scope. The write-ahead log is read
     * as well as the main file: WAL is the default journal here, so a credential
     * that had been written and not yet checkpointed would be in the sidecar and
     * a scan of the main file alone would pass while the secret sat on disk
     * beside it.
     */
    const auto main_file = read_bytes(scratch.path());
    REQUIRE_FALSE(main_file.empty());

    std::string everything = main_file;
    const auto wal_path = scratch.path().string() + "-wal";
    if (std::filesystem::exists(wal_path))
        everything += read_bytes(wal_path);

    REQUIRE(everything.find(password) == std::string::npos);

    /*
     * And the plaintext credential is absent too, which is the other half of
     * the claim: the password protects the file, and the credential in it is
     * sealed rather than merely obscured.
     */
    REQUIRE(everything.find("the-secret-credential") == std::string::npos);

    /*
     * The seal really did happen, so the two assertions above are not passing
     * because nothing was written at all. The sealed value is in the file as
     * base64 and the credential column is not empty.
     */
    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());
    account_repository accounts(reopened.value());
    const auto stored = accounts.credential_of("acct-1");
    REQUIRE(stored.has_value());
    REQUIRE(stored.value().has_value());
    REQUIRE_FALSE(stored.value().value().empty());

    const auto opened =
        secret_box::open(password, stored.value().value());
    REQUIRE(opened.has_value());
    REQUIRE(opened.value() == "the-secret-credential");
}

TEST_CASE("a fresh store has an empty master table", "[schema]") {
    const temporary_database scratch;

    auto opened = database::open(scratch.path());
    REQUIRE(opened.has_value());
    REQUIRE(opened.value().apply_schema().has_value());

    master_repository masters(opened.value());
    REQUIRE_FALSE(masters.has_master().value());
    REQUIRE(masters.verification().value().empty());
}
