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
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <system_error>
#include <thread>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::secret_string;
using hedgr::model::domain::instance;
using hedgr::model::domain::store_lock;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * The lock state lives in the file rather than in the manager: whether a store
 * is absent or locked is answered by a row it does or does not hold, and the
 * only way to test that a restart reads it back is to have a file to reopen.
 */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.runtime.lock.{}.{}.sqlite", unique_token(),
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

/** @brief The options the application builds from the environment. */
[[nodiscard]] session_options seed_options() {
    session_options options;
    options.nats_url = "nats://localhost:20405";
    options.subject_prefix = "ores.dev.brave_hopper";
    return options;
}

/**
 * @brief Pumps the manager until @p ready says so, or the limit passes.
 *
 * The unlock is a coroutine on the runtime's pool by design, so its outcome does
 * not arrive on the frame that pushed the command: it arrives as an event, some
 * frames later. A bounded wait rather than a sleep, so a failure fails the test
 * instead of hanging the suite on a worker that never ran.
 */
[[nodiscard]] bool pump_until(instance_manager& manager,
    const std::function<bool()>& ready, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        manager.pump();
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    manager.pump();
    return ready();
}

/** @brief Pushes one command carrying @p password and pumps until @p ready. */
[[nodiscard]] bool run_until(instance_manager& manager, command_kind kind,
    std::string password, const std::function<bool()>& ready) {
    command request;
    request.kind = kind;
    request.password = secret_string{std::move(password)};
    manager.commands().push(std::move(request));
    return pump_until(manager, ready, std::chrono::seconds(20));
}

/** @brief A distinctive master password, long enough to be a real one. */
constexpr std::string_view master_password =
    "correct horse battery staple, and then a second horse";

}

TEST_CASE("a store with no master row starts absent", "[store_lock]") {
    const temporary_database database;
    instance_manager manager;

    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.store_lock() == store_lock::absent);
    REQUIRE(manager.last_error().empty());
    REQUIRE_FALSE(manager.is_unlocking());
}

TEST_CASE("creating a master password unlocks the store", "[store_lock]") {
    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    const auto reached = run_until(manager, command_kind::set_master_password,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; });

    REQUIRE(reached);
    REQUIRE(manager.store_lock() == store_lock::unlocked);
    REQUIRE(manager.lock_error().empty());
    REQUIRE_FALSE(manager.is_unlocking());
}

TEST_CASE("a store with a master row starts locked", "[store_lock]") {
    const temporary_database database;

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    /*
     * A second run, and the password is not held: the row is what says the
     * store has a master password, and the absence of the password from memory
     * is what says it is not held. This is the state the interface renders as a
     * closed padlock and asks about.
     */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.store_lock() == store_lock::locked);
    REQUIRE_FALSE(reopened.is_unlocking());
}

TEST_CASE("unlocking with the right password reports store_unlocked",
    "[store_lock]") {
    const temporary_database database;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.store_lock() == store_lock::locked);

    const auto reached = run_until(manager, command_kind::unlock_store,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; });

    REQUIRE(reached);
    REQUIRE(manager.store_lock() == store_lock::unlocked);
    REQUIRE(manager.lock_error().empty());
}

TEST_CASE("unlocking with a wrong password reports store_lock_failed",
    "[store_lock]") {
    const temporary_database database;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    const auto reached = run_until(manager, command_kind::unlock_store,
        "not-the-master-password",
        [&manager] { return manager.store_lock() == store_lock::failed; });

    REQUIRE(reached);
    REQUIRE(manager.store_lock() == store_lock::failed);

    /*
     * The sentence is shown beside the prompt and says the password was not
     * accepted. It never quotes what was typed, which is the reason a refusal
     * can be shown at all.
     */
    REQUIRE(manager.lock_error() == "the master password is not correct");
    REQUIRE(manager.lock_error().find("not-the-master-password")
        == std::string::npos);

    /*
     * And the store is still there to try again: a refused attempt is a state,
     * not a failure that leaves the application unusable.
     */
    const auto retried = run_until(manager, command_kind::unlock_store,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; });
    REQUIRE(retried);
    REQUIRE(manager.store_lock() == store_lock::unlocked);
    REQUIRE(manager.lock_error().empty());
}

TEST_CASE("locking drops the password and returns the store to locked",
    "[store_lock]") {
    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(run_until(manager, command_kind::set_master_password,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; }));

    command lock;
    lock.kind = command_kind::lock_store;
    manager.commands().push(std::move(lock));
    manager.pump();

    REQUIRE(manager.store_lock() == store_lock::locked);

    /*
     * Locking is the operator's interest and always lands on @c locked, never
     * on @c failed: the failed state describes a refused attempt, and nobody
     * attempted anything here.
     */
    REQUIRE(manager.lock_error().empty());

    /* And the password is genuinely gone, so unlocking is a fresh derivation. */
    const auto wrong = run_until(manager, command_kind::unlock_store,
        "the-password-that-was-cleared",
        [&manager] { return manager.store_lock() == store_lock::failed; });
    REQUIRE(wrong);

    const auto right = run_until(manager, command_kind::unlock_store,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; });
    REQUIRE(right);
}

TEST_CASE("setting a master password when one exists is refused and keeps it",
    "[store_lock]") {
    const temporary_database database;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.store_lock() == store_lock::locked);

    command request;
    request.kind = command_kind::set_master_password;
    request.password = secret_string{std::string{"a-different-password"}};
    manager.commands().push(std::move(request));
    REQUIRE_FALSE(pump_until(manager,
        [&manager] { return manager.store_lock() != store_lock::locked; },
        std::chrono::milliseconds(300)));

    REQUIRE(manager.store_lock() == store_lock::locked);
    REQUIRE_FALSE(manager.is_unlocking());
    REQUIRE(manager.last_error().find("already has a master password")
        != std::string::npos);

    REQUIRE(run_until(manager, command_kind::unlock_store,
        "a-different-password",
        [&manager] { return manager.store_lock() == store_lock::failed; }));
    REQUIRE(run_until(manager, command_kind::unlock_store,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; }));
}

TEST_CASE("a refused unlock leaves no password held for the next attempt",
    "[store_lock]") {
    const temporary_database database;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(run_until(manager, command_kind::unlock_store, "wrong-one",
        [&manager] { return manager.store_lock() == store_lock::failed; }));

    command request;
    request.kind = command_kind::lock_store;
    manager.commands().push(std::move(request));
    manager.pump();
    REQUIRE(manager.store_lock() == store_lock::locked);
    REQUIRE_FALSE(manager.is_unlocking());
}

TEST_CASE("a credential typed for an attempt is used without a stored one",
    "[store_lock]") {
    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    /*
     * The store is absent and a password is supplied anyway. That is a typed
     * credential and it must be used: this is the path an operator takes on a
     * fresh store, before any master password exists at all.
     */
    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = std::string{manager.instances().rows()[0].id};
    connect.account_id = "acct-1";
    connect.principal = "ops@host";
    connect.password = secret_string{std::string{"a-typed-password"}};

    /* The account has to exist, so it is created first. */
    account row;
    row.id = "acct-1";
    row.instance_id = connect.instance_id;
    row.name = "ops read-only";
    row.principal = "ops@host";

    command save;
    save.kind = command_kind::save_account;
    save.account_row = row;
    manager.commands().push(std::move(save));
    manager.pump();
    /*
     * The two logins the start seeded are still there, so this account makes
     * three. What the case needs is that acct-1 exists, not that it is alone.
     */
    REQUIRE(manager.accounts().size() == 3);
    REQUIRE(manager.accounts().find("acct-1").has_value());

    /*
     * The ids are kept, because the command below is moved into the queue and a
     * moved-from command is not a thing to read an id back out of.
     */
    const auto instance_id = connect.instance_id;

    manager.commands().push(std::move(connect));
    manager.pump();

    /*
     * The attempt reached the session table, which is all this test claims:
     * whether the broker answers is the transport's business. What matters
     * here is that a typed password with a closed lock is not refused for being
     * locked.
     */
    const auto session = manager.sessions().find(instance_id, "acct-1");
    REQUIRE(session.has_value());
}

TEST_CASE("a connect with no password and a locked store names the lock",
    "[store_lock]") {
    const temporary_database database;

    std::string instance_id;
    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        instance_id = std::string{manager.instances().rows()[0].id};

        command save;
        save.kind = command_kind::save_account;
        account row;
        row.id = "acct-1";
        row.instance_id = instance_id;
        row.name = "ops read-only";
        row.principal = "ops@host";
        save.account_row = row;
        manager.commands().push(std::move(save));
        manager.pump();

        REQUIRE(run_until(manager, command_kind::set_master_password,
            std::string{master_password},
            [&manager] { return manager.store_lock() == store_lock::unlocked; }));
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.store_lock() == store_lock::locked);

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = instance_id;
    connect.account_id = "acct-1";
    connect.principal = "ops@host";
    manager.commands().push(std::move(connect));
    manager.pump();

    /*
     * The refusal has to say the store is locked rather than that the password
     * was wrong. An operator who is told their password is bad will retype a
     * password that was never the problem, and there was no password field in
     * front of them to retype it into.
     */
    REQUIRE(manager.last_error().find("locked") != std::string::npos);
    REQUIRE(manager.last_error().find("password was wrong") == std::string::npos);
    REQUIRE(manager.last_error().find(master_password) == std::string::npos);
}

TEST_CASE("a connect with no password and no stored credential is refused",
    "[store_lock]") {
    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());

    const auto instance_id = std::string{manager.instances().rows()[0].id};

    command save;
    save.kind = command_kind::save_account;
    account row;
    row.id = "acct-1";
    row.instance_id = instance_id;
    row.name = "ops read-only";
    row.principal = "ops@host";
    save.account_row = row;
    manager.commands().push(std::move(save));
    manager.pump();

    REQUIRE(run_until(manager, command_kind::set_master_password,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; }));

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = instance_id;
    connect.account_id = "acct-1";
    connect.principal = "ops@host";
    manager.commands().push(std::move(connect));
    manager.pump();

    /* Unlocked, but this account has no stored credential and none was typed. */
    REQUIRE(manager.last_error().find("no stored credential")
        != std::string::npos);
}

TEST_CASE("a stored credential is opened and used without a typed password",
    "[store_lock]") {
    const temporary_database database;
    const std::string instance_id = "inst-stored";

    /*
     * The store is prepared out of band, with the credential sealed the way the
     * runtime would seal one. This is what a second launch looks like: the row
     * is on disk and the operator supplies only the master password.
     */
    {
        auto opened = hedgr::store::sqlite::database::open(database.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened.value().apply_schema().has_value());
        auto& db = opened.value();

        hedgr::store::repository::instance_repository instances(db);
        instance seeded;
        seeded.id = instance_id;
        seeded.name = "Stored";
        seeded.nats_url = "nats://localhost:20405";
        seeded.subject_prefix = "ores.dev.brave_hopper";
        seeded.wire_format = "msgpack";
        REQUIRE(instances.save(seeded).has_value());

        hedgr::store::repository::account_repository accounts(db);
        account row;
        row.id = "acct-1";
        row.instance_id = instance_id;
        row.name = "ops read-only";
        row.principal = "ops@host";
        REQUIRE(accounts.save(row).has_value());

        const auto sealed = hedgr::store::crypto::secret_box::seal(
            master_password, "the-stored-credential");
        REQUIRE(sealed.has_value());
        REQUIRE(accounts.set_credential("acct-1", sealed.value()).has_value());

        hedgr::store::repository::master_repository masters(db);
        REQUIRE(masters.create(master_password).has_value());
    }

    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.store_lock() == store_lock::locked);

    REQUIRE(run_until(manager, command_kind::unlock_store,
        std::string{master_password},
        [&manager] { return manager.store_lock() == store_lock::unlocked; }));

    command connect;
    connect.kind = command_kind::connect_session;
    connect.instance_id = instance_id;
    connect.account_id = "acct-1";
    connect.principal = "ops@host";

    /* No password at all: the store is expected to supply one. */
    manager.commands().push(std::move(connect));

    /*
     * The credential is opened on the pool and the session is started from
     * there, so the attempt arrives as a command on a later frame. This waits
     * for the session row the attempt creates.
     */
    const auto reached = pump_until(manager,
        [&manager, &instance_id] {
            return manager.sessions().find(instance_id, "acct-1").has_value();
        },
        std::chrono::seconds(20));

    REQUIRE(reached);
    REQUIRE(manager.sessions().find(instance_id, "acct-1").has_value());

    /*
     * Nothing about the failure path leaked a secret into the banner, whatever
     * the broker answered: this test is about the credential, not the deployment.
     */
    REQUIRE(manager.last_error().find(master_password) == std::string::npos);
    REQUIRE(manager.last_error().find("the-stored-credential")
        == std::string::npos);
}
