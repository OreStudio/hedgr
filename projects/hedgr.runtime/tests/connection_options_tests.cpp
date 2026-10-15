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
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

using hedgr::model::domain::instance;
using hedgr::runtime::manager::connection_options_for;
using hedgr::runtime::manager::resolve_tls_path;
using hedgr::session::transport::session_options;

/** @brief A store file, so a relative path has a folder to resolve against. */
const std::filesystem::path store_path{"/srv/hedgr/store/hedgr.sqlite"};

/** @brief The material a process started with TLS in its environment holds. */
[[nodiscard]] session_options environment_seed() {
    session_options seed;
    seed.nats_url = "nats://localhost:20405";
    seed.subject_prefix = "ores.dev.brave_hopper";
    seed.tls_ca = "/opt/ores/keys/nats/ca.crt";
    seed.tls_cert = "/opt/ores/keys/nats/ores.shell.crt";
    seed.tls_key = "/opt/ores/keys/nats/ores.shell.key";
    seed.request_timeout = std::chrono::milliseconds(1500);
    return seed;
}

/** @brief An instance row pointing at the deployment the seed names. */
[[nodiscard]] instance instance_row() {
    instance row;
    row.id = "inst-1";
    row.name = "Brave Hopper";
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = "ores.dev.brave_hopper";
    return row;
}

}

TEST_CASE("a relative TLS path resolves against the folder holding the store",
    "[runtime][connection]") {
    REQUIRE(resolve_tls_path("keys/nats/ca.crt", store_path) ==
        "/srv/hedgr/store/keys/nats/ca.crt");
    REQUIRE(resolve_tls_path("./ca.crt", store_path) == "/srv/hedgr/store/ca.crt");
    REQUIRE(resolve_tls_path("keys/../keys/ca.crt", store_path) ==
        "/srv/hedgr/store/keys/ca.crt");
}

TEST_CASE("an absolute TLS path is left exactly as it was written",
    "[runtime][connection]") {
    REQUIRE(resolve_tls_path("/etc/ssl/certs/ca.crt", store_path) ==
        "/etc/ssl/certs/ca.crt");
}

TEST_CASE("an empty TLS path stays empty rather than becoming the store's folder",
    "[runtime][connection]") {
    REQUIRE(resolve_tls_path("", store_path).empty());
}

TEST_CASE("a store with no folder leaves a relative path alone",
    "[runtime][connection]") {
    /*
     * An in-memory store has no file to be beside. Resolving against it would
     * have to invent a directory, and a path nobody wrote is worse than a
     * relative path the process's own working directory decides.
     */
    REQUIRE(resolve_tls_path("keys/ca.crt", std::filesystem::path{}) ==
        "keys/ca.crt");
}

TEST_CASE("an instance with its own TLS paths connects with them",
    "[runtime][connection]") {
    auto row = instance_row();
    row.tls_ca = "keys/nats/ca.crt";
    row.tls_cert = "keys/nats/ores.shell.crt";
    row.tls_key = "/home/operator/private/ores.shell.key";

    const auto options = connection_options_for(row, environment_seed(), store_path);

    REQUIRE(options.nats_url == "nats://localhost:20405");
    REQUIRE(options.subject_prefix == "ores.dev.brave_hopper");
    REQUIRE(options.tls_ca == "/srv/hedgr/store/keys/nats/ca.crt");
    REQUIRE(options.tls_cert == "/srv/hedgr/store/keys/nats/ores.shell.crt");
    /* Absolute, so it is the operator's own path and not the store's. */
    REQUIRE(options.tls_key == "/home/operator/private/ores.shell.key");
}

TEST_CASE("an instance with no TLS paths connects with the environment's",
    "[runtime][connection]") {
    const auto options =
        connection_options_for(instance_row(), environment_seed(), store_path);

    REQUIRE(options.tls_ca == "/opt/ores/keys/nats/ca.crt");
    REQUIRE(options.tls_cert == "/opt/ores/keys/nats/ores.shell.crt");
    REQUIRE(options.tls_key == "/opt/ores/keys/nats/ores.shell.key");
    REQUIRE(options.uses_tls());
}

TEST_CASE("each empty path falls back on its own", "[runtime][connection]") {
    /*
     * Not all-or-nothing: a deployment behind a different authority is trusted
     * with one path, while the client identity the machine already has is
     * presented from the rest.
     */
    auto row = instance_row();
    row.tls_ca = "keys/nats/ca.crt";

    const auto options = connection_options_for(row, environment_seed(), store_path);

    REQUIRE(options.tls_ca == "/srv/hedgr/store/keys/nats/ca.crt");
    REQUIRE(options.tls_cert == "/opt/ores/keys/nats/ores.shell.crt");
    REQUIRE(options.tls_key == "/opt/ores/keys/nats/ores.shell.key");
}

TEST_CASE("an instance naming no CA at all connects in the clear",
    "[runtime][connection]") {
    /*
     * The CA is the switch, so a row with only a certificate and a key -- or an
     * environment that supplied neither -- is a connection with no TLS, which
     * is what @c session_options documents and what a test of the fallback
     * above must not be confused with.
     */
    session_options bare;
    bare.nats_url = "nats://localhost:20405";
    bare.subject_prefix = "ores.dev.brave_hopper";

    const auto options = connection_options_for(instance_row(), bare, store_path);

    REQUIRE_FALSE(options.uses_tls());
    REQUIRE(options.tls_ca.empty());
    REQUIRE(options.tls_cert.empty());
    REQUIRE(options.tls_key.empty());
}

TEST_CASE("the timeouts are the client's and never the row's",
    "[runtime][connection]") {
    const auto seed = environment_seed();
    const auto options = connection_options_for(instance_row(), seed, store_path);

    REQUIRE(options.request_timeout == std::chrono::milliseconds(1500));
    REQUIRE(options.connect_timeout == seed.connect_timeout);
}
