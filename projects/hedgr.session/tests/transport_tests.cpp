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

/*
 * There is deliberately no test here that talks to a live NATS server. These
 * tests must pass on a machine with no broker, no certificates and no IAM
 * service, because that is the machine a developer runs them on and a green
 * suite that quietly depends on a daemon is worse than no suite. What is
 * testable without a server is the part that is worth pinning down anyway: the
 * subject arithmetic, the TLS switch, the state a fresh session reports, and
 * the translation of a transport failure into the sentence a person reads.
 * The round trips themselves are covered by the running application against a
 * real deployment.
 */

#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_all.hpp>
#include <stdexcept>
#include <string>
#include "ores.nats/service/nats_connect_error.hpp"

using hedgr::session::transport::make_subject;
using hedgr::session::transport::session_client;
using hedgr::session::transport::session_errc;
using hedgr::session::transport::session_options;
using hedgr::session::transport::translate_connect_error;

namespace {

/**
 * @brief Options for one instance, so each test states only what it is about.
 */
[[nodiscard]] session_options options_for(const std::string& prefix) {
    return session_options{
        .nats_url = "nats://localhost:20405",
        .subject_prefix = prefix};
}

}

TEST_CASE("a subject is the instance prefix joined to the suffix", "[transport]") {
    const auto options = options_for("ores.dev.brave_hopper");

    REQUIRE(make_subject(options, "iam.v1.ops.login") ==
        "ores.dev.brave_hopper.iam.v1.ops.login");
}

TEST_CASE("a prefix that already ends in a dot is not doubled", "[transport]") {
    const auto options = options_for("ores.dev.brave_hopper.");

    REQUIRE(make_subject(options, "iam.v1.ops.login") ==
        "ores.dev.brave_hopper.iam.v1.ops.login");
}

TEST_CASE("an instance with no prefix leaves the suffix alone", "[transport]") {
    const auto options = options_for("");

    REQUIRE(make_subject(options, "iam.v1.ops.login") == "iam.v1.ops.login");
}

TEST_CASE("TLS is off until a CA to trust is given", "[transport]") {
    const auto plain = options_for("ores.dev.brave_hopper");
    REQUIRE_FALSE(plain.uses_tls());

    auto secure = plain;
    secure.tls_ca = "build/keys/nats/ca.crt";
    secure.tls_cert = "build/keys/nats/ores.shell.crt";
    secure.tls_key = "build/keys/nats/ores.shell.key";
    REQUIRE(secure.uses_tls());

    /* A cert and key without a CA would connect in the clear, so they do not
     * turn TLS on by themselves. */
    auto half = plain;
    half.tls_cert = "build/keys/nats/ores.shell.crt";
    half.tls_key = "build/keys/nats/ores.shell.key";
    REQUIRE_FALSE(half.uses_tls());
}

TEST_CASE("a session that was never connected reports itself so", "[transport]") {
    const session_client session(options_for("ores.dev.brave_hopper"));

    REQUIRE_FALSE(session.is_connected());
    REQUIRE_FALSE(session.is_authenticated());
    REQUIRE(session.identity().token.empty());
    REQUIRE(session.options().subject_prefix == "ores.dev.brave_hopper");
}

TEST_CASE("a request on an unconnected session fails instead of throwing",
    "[transport]") {
    const session_client session(options_for("ores.dev.brave_hopper"));

    const auto reply = session.authenticated_request("iam.v1.ops.logout", "{}");

    REQUIRE_FALSE(reply.has_value());
    REQUIRE_FALSE(reply.error().message.empty());
}

TEST_CASE("disconnecting a session that was never connected is safe",
    "[transport]") {
    session_client session(options_for("ores.dev.brave_hopper"));

    session.disconnect();
    session.disconnect();

    REQUIRE_FALSE(session.is_connected());
    REQUIRE_FALSE(session.is_authenticated());
}

TEST_CASE("a broken service behind the broker reads as the instance being down",
    "[transport]") {
    const ores::nats::service::nats_connect_error refused(
        ores::nats::service::nats_error_kind::services_unavailable,
        "NATS request failed: no service is handling subject "
        "'ores.dev.brave_hopper.iam.v1.ops.login'");

    const auto error = translate_connect_error(refused);

    REQUIRE(error.code == session_errc::services_unavailable);
    REQUIRE(error.message ==
        "the instance is not responding; contact your system administrator");
    REQUIRE(error.detail == refused.what());
}

TEST_CASE("an unreachable broker reads as the instance being unreachable",
    "[transport]") {
    const ores::nats::service::nats_connect_error refused(
        ores::nats::service::nats_error_kind::server_unreachable,
        "NATS connect failed: No servers available");

    const auto error = translate_connect_error(refused);

    REQUIRE(error.code == session_errc::unreachable);
    REQUIRE(error.detail == "NATS connect failed: No servers available");
}

TEST_CASE("a failure the transport cannot name is a protocol error",
    "[transport]") {
    const std::runtime_error other("NATS publish failed: Connection closed");

    const auto error = translate_connect_error(other);

    REQUIRE(error.code == session_errc::protocol_error);
    REQUIRE(error.detail == "NATS publish failed: Connection closed");
}
