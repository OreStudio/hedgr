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
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using hedgr::model::domain::folder;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::label_colour;
using hedgr::model::domain::label_severity;
using hedgr::model::domain::labelling_owner;
using hedgr::runtime::manager::instance_manager;
using hedgr::session::transport::session_options;

/**
 * @brief A directory of checkouts that stands in for the operator's machine.
 *
 * The seeder reads a directory of @c ores_dev_* checkouts, each with a @c .env.
 * A test that used the real one would pass or fail depending on how many
 * deployments happen to be beside the repository, so it makes its own and
 * points @c HEDGR_ORESTUDIO_ROOT at it. The variable is put back when the
 * fixture goes away, so the cases that are not about the scan still see the
 * value the suite was started with.
 */
class checkout_directory final {
public:
    checkout_directory()
        : root_(std::filesystem::temp_directory_path() /
              std::format("hedgr.env.{}.{}", unique_token(), next_serial())) {
        std::filesystem::create_directories(root_);
        /*
         * Put back whatever the suite was started with when this goes away.
         * Unsetting would look equivalent and is not: the suite pins the
         * variable to nothing so the seeding cases do not read the operator's
         * machine, and an unset variable falls through to the directory baked
         * in at configure time, which is the machine again.
         */
        if (const char* previous = std::getenv("HEDGR_ORESTUDIO_ROOT");
            previous != nullptr)
            previous_root_ = previous;
        setenv("HEDGR_ORESTUDIO_ROOT", root_.c_str(), 1);
    }

    ~checkout_directory() {
        if (previous_root_)
            setenv("HEDGR_ORESTUDIO_ROOT", previous_root_->c_str(), 1);
        else
            unsetenv("HEDGR_ORESTUDIO_ROOT");
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    checkout_directory(const checkout_directory&) = delete;
    checkout_directory& operator=(const checkout_directory&) = delete;

    /**
     * @brief Writes a checkout whose @c .env states the variables the seeder
     * reads, TLS material included.
     */
    void add(std::string_view directory, std::string_view label,
        std::string_view url, std::string_view prefix) {
        const auto path = root_ / directory;
        std::filesystem::create_directories(path);
        std::ofstream file(path / ".env");
        file << "# a fixture, not a deployment\n"
             << "ORES_ENV_VERSION=31\n"
             << "ORES_ENV_NAME=" << label << "\n"
             << "ORES_CHECKOUT_LABEL=" << label << "\n"
             << "ORES_ENV_TYPE=development\n"
             << "ORES_NATS_URL=" << url << "\n"
             << "ORES_NATS_SUBJECT_PREFIX=" << prefix << "\n"
             << "ORES_NATS_WIRE_FORMAT=msgpack\n"
             << tls_lines(label);
    }

    /**
     * @brief Writes a checkout whose @c .env names a broker but no TLS.
     *
     * The shape a checkout has before its certificates are provisioned. The
     * seeded row must still be usable, which means the three paths stay empty
     * rather than being filled with something invented.
     */
    void add_without_tls(std::string_view directory, std::string_view label,
        std::string_view url, std::string_view prefix) {
        const auto path = root_ / directory;
        std::filesystem::create_directories(path);
        std::ofstream file(path / ".env");
        file << "ORES_CHECKOUT_LABEL=" << label << "\n"
             << "ORES_ENV_TYPE=development\n"
             << "ORES_NATS_URL=" << url << "\n"
             << "ORES_NATS_SUBJECT_PREFIX=" << prefix << "\n";
    }

    /** @brief Writes a checkout with no @c .env at all. */
    void add_without_env(std::string_view directory) {
        std::filesystem::create_directories(root_ / directory);
    }

    /** @brief Writes a checkout whose @c .env names no broker. */
    void add_without_broker(std::string_view directory) {
        const auto path = root_ / directory;
        std::filesystem::create_directories(path);
        std::ofstream file(path / ".env");
        file << "ORES_CHECKOUT_LABEL=half_built\nORES_ENV_TYPE=development\n";
    }

    /** @brief A directory that is not a checkout at all. */
    void add_unrelated(std::string_view directory) {
        std::filesystem::create_directories(root_ / directory);
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    /**
     * @brief The TLS lines one fixture checkout carries, keyed by its label.
     *
     * Derived from the label rather than fixed, so a case can assert on an
     * exact path and tell one checkout's material from another's. They are
     * absolute, which is how a deployment's own @c .env states them, and the
     * files need not exist: the seeder records what it was told, and opening
     * them is the transport's business.
     */
    [[nodiscard]] static std::string tls_lines(std::string_view label) {
        return std::format(
            "ORES_NATS_TLS_CA=/keys/{0}/nats/ca.crt\n"
            "ORES_NATS_TLS_CERT=/keys/{0}/nats/ores.shell.crt\n"
            "ORES_NATS_TLS_KEY=/keys/{0}/nats/ores.shell.key\n",
            label);
    }

    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    std::filesystem::path root_;
    std::optional<std::string> previous_root_;
};

/** @brief A database file that deletes itself, sidecars included. */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.environment.{}.{}.sqlite", unique_token(),
                  next_serial())) {
        erase();
    }

    ~temporary_database() { erase(); }

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

session_options seed_options() {
    return session_options{
        .nats_url = "nats://localhost:20405",
        .subject_prefix = "ores.dev.brave_hopper",
    };
}

}

TEST_CASE("a seeded store holds one instance per environment", "[seed]") {
    checkout_directory checkouts;
    checkouts.add("ores_dev_brave_hopper", "brave_hopper",
        "nats://localhost:20405", "ores.dev.brave_hopper");
    checkouts.add("ores_dev_swift_curie", "swift_curie",
        "nats://localhost:20205", "ores.dev.swift_curie");
    checkouts.add("ores_dev_festive_dijkstra", "festive_dijkstra",
        "nats://localhost:21805", "ores.dev.festive.dijkstra");
    /* Not a checkout: no ores_dev_ prefix, so the seeder must not see it. */
    checkouts.add_unrelated("ores.tmp");

    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());

    REQUIRE(manager.instances().size() == 3);

    const auto instance = [&manager](std::string_view id) {
        const auto handle = manager.instances().find(id);
        REQUIRE(handle.has_value());
        return *manager.instances().get(*handle);
    };

    /*
     * The id is derived from the checkout's label, so the row a second seeding
     * would write is the row this one did. The name is the label with its
     * underscores turned into spaces and title-cased, which is what the tree
     * draws.
     */
    const auto brave = instance("inst-env-brave_hopper");
    REQUIRE(brave.name == "Brave Hopper");
    REQUIRE(brave.nats_url == "nats://localhost:20405");
    REQUIRE(brave.subject_prefix == "ores.dev.brave_hopper");
    REQUIRE(brave.wire_format == "msgpack");
    REQUIRE_FALSE(brave.created_at.empty());

    /*
     * The illustration is named by the checkout's label, which is the same word
     * the environment is known by everywhere else. It is a key and not the
     * image, so each of the ten environments points at one asset rather than
     * carrying a copy of it.
     */
    REQUIRE(brave.icon_key == "brave_hopper");

    /*
     * The checkout's own certificates, written into the row. That is what makes
     * a store created on this host work on it without anything being typed in,
     * and it is the deployment's answer rather than one this process supplied.
     */
    REQUIRE(brave.tls_ca == "/keys/brave_hopper/nats/ca.crt");
    REQUIRE(brave.tls_cert == "/keys/brave_hopper/nats/ores.shell.crt");
    REQUIRE(brave.tls_key == "/keys/brave_hopper/nats/ores.shell.key");

    const auto swift = instance("inst-env-swift_curie");
    REQUIRE(swift.name == "Swift Curie");
    REQUIRE(swift.nats_url == "nats://localhost:20205");
    REQUIRE(swift.icon_key == "swift_curie");

    const auto festive = instance("inst-env-festive_dijkstra");
    REQUIRE(festive.name == "Festive Dijkstra");
    REQUIRE(festive.subject_prefix == "ores.dev.festive.dijkstra");

    /*
     * Every one of these checkouts is a development deployment, so they belong
     * in a Development folder under the instances root rather than at the root.
     */
    const auto development = manager.folders().find("folder-development");
    REQUIRE(development.has_value());
    const auto& folder_row = *manager.folders().get(*development);
    REQUIRE(folder_row.name == "Development");
    REQUIRE(folder_row.tree == folder_tree::instances);
    REQUIRE(folder_row.parent_id == hedgr::model::domain::instances_root_id);

    for (const auto& row : manager.instances().rows()) {
        INFO("instance " << row.id);
        REQUIRE(row.folder_id == "folder-development");
    }

    /* And each carries the one label that names their tier. */
    const auto label = manager.labels().find("label-development");
    REQUIRE(label.has_value());
    const auto& label_row = *manager.labels().get(*label);
    REQUIRE(label_row.name == "development");
    REQUIRE(label_row.colour == label_colour::blue);
    REQUIRE(label_row.severity == label_severity::none);

    for (const auto& row : manager.instances().rows()) {
        INFO("instance " << row.id);
        const auto attached = manager.labellings().labels_of(
            labelling_owner::instance, row.id);
        REQUIRE(attached.size() == 1);
        REQUIRE(attached.front() == "label-development");
    }

    manager.stop();
}

TEST_CASE("seeding a store twice leaves one instance per environment",
    "[seed]") {
    checkout_directory checkouts;
    checkouts.add("ores_dev_brave_hopper", "brave_hopper",
        "nats://localhost:20405", "ores.dev.brave_hopper");
    checkouts.add("ores_dev_jolly_knuth", "jolly_knuth",
        "nats://localhost:20805", "ores.dev.jolly_knuth");

    const temporary_database database;

    {
        instance_manager manager;
        REQUIRE(manager.start(database.path(), seed_options()).has_value());
        REQUIRE(manager.instances().size() == 2);
        manager.stop();
    }

    /*
     * A second manager over the same file, which is what a restart is. The seed
     * is skipped because the store already holds rows, and even if it were not,
     * every id is derived from a label: the row would be replaced by itself
     * rather than added beside it.
     */
    instance_manager reopened;
    REQUIRE(reopened.start(database.path(), seed_options()).has_value());
    REQUIRE(reopened.instances().size() == 2);
    REQUIRE(reopened.folders().find("folder-development").has_value());
    REQUIRE(reopened.labels().find("label-development").has_value());
    REQUIRE(reopened.labellings().size() == 2);
    REQUIRE(reopened.instances().find("inst-env-brave_hopper").has_value());
    REQUIRE(reopened.instances().find("inst-env-jolly_knuth").has_value());

    /* And a third, because two restarts is where an accumulating bug shows. */
    instance_manager again;
    REQUIRE(again.start(database.path(), seed_options()).has_value());
    REQUIRE(again.instances().size() == 2);
    REQUIRE(again.labellings().size() == 2);

    again.stop();
    reopened.stop();
}

TEST_CASE("a checkout the seeder cannot read is skipped, not fatal", "[seed]") {
    checkout_directory checkouts;
    checkouts.add("ores_dev_brave_hopper", "brave_hopper",
        "nats://localhost:20405", "ores.dev.brave_hopper");
    checkouts.add_without_env("ores_dev_no_env");
    checkouts.add_without_broker("ores_dev_half_built");

    const temporary_database database;
    instance_manager manager;

    /*
     * The seeding is a convenience. A store that would not start because one
     * checkout beside it is half-built would be a worse failure than one that
     * starts a row short.
     */
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());
    REQUIRE(manager.instances().size() == 1);
    REQUIRE(manager.instances().find("inst-env-brave_hopper").has_value());
    REQUIRE_FALSE(manager.instances().find("inst-env-no_env").has_value());
    REQUIRE_FALSE(manager.instances().find("inst-env-half_built").has_value());

    manager.stop();
}

TEST_CASE("a store with no checkouts beside it keeps the single seed", "[seed]") {
    checkout_directory checkouts;
    checkouts.add_unrelated("something-else");

    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());

    /*
     * A deployed package has no source tree to scan. It still comes up with
     * somewhere to point, which is the one instance its own options describe
     * rather than nothing at all.
     */
    REQUIRE(manager.instances().size() == 1);
    const auto& row = manager.instances().rows()[0];
    REQUIRE(row.name == "local development");
    REQUIRE(row.nats_url == "nats://localhost:20405");
    REQUIRE(row.folder_id == hedgr::model::domain::instances_root_id);

    /*
     * No checkout means no label and therefore no illustration. Empty is the
     * stored value that says so, and the interface draws its own default rather
     * than the seeder guessing at a picture for an instance that was never an
     * environment.
     */
    REQUIRE(row.icon_key.empty());

    manager.stop();
}

TEST_CASE("an environment that names no TLS leaves the fallback to answer",
    "[seed]") {
    checkout_directory checkouts;
    checkouts.add_without_tls("ores_dev_brave_hopper", "brave_hopper",
        "nats://localhost:20405", "ores.dev.brave_hopper");

    const temporary_database database;
    instance_manager manager;
    REQUIRE(manager.start(database.path(), seed_options()).has_value());
    REQUIRE(manager.last_error().empty());

    /*
     * Empty rather than filled with something invented: the row says nothing,
     * and the runtime reads that as "the environment answers". Writing a path
     * the checkout never named would be this seeder guessing at a certificate.
     */
    const auto handle = manager.instances().find("inst-env-brave_hopper");
    REQUIRE(handle.has_value());
    const auto& row = *manager.instances().get(*handle);
    REQUIRE(row.tls_ca.empty());
    REQUIRE(row.tls_cert.empty());
    REQUIRE(row.tls_key.empty());

    manager.stop();
}
