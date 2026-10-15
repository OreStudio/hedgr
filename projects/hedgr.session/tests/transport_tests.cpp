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
 * subject arithmetic, the TLS switch, the state a fresh session reports, the
 * sentence a person reads when a certificate cannot be opened, and the
 * translation of a transport failure into the sentence a person reads.
 * The round trips themselves are covered by the running application against a
 * real deployment.
 */

#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
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

/**
 * @brief A file that exists for the length of one case and then removes itself.
 *
 * The TLS check only asks whether a path can be opened, so a test that needs a
 * readable CA needs a real file and nothing in it. The name carries a per-run
 * token, so a file left behind by a killed run can never be picked up by the
 * next one.
 */
class scratch_file final {
public:
    scratch_file(std::string name, std::string_view contents)
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.session.{}.{}", unique_token(), name)) {
        std::ofstream file(path_);
        file << contents;
    }

    ~scratch_file() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    scratch_file(const scratch_file&) = delete;
    scratch_file& operator=(const scratch_file&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}", source());
    }

    std::filesystem::path path_;
};

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
    REQUIRE(session.account().token.empty());
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

TEST_CASE("a certificate that cannot be opened is named in the failure",
    "[transport]") {
    /*
     * A mistyped path and an unreachable broker both end as a connect that
     * never opened, and the two need different fixes. The failure therefore
     * names the file, and it is produced without a broker on the machine
     * because the file is read before the client is handed it.
     */
    const std::string missing = "/nonexistent/hedgr-tls/ca.crt";

    auto secured = options_for("ores.dev.brave_hopper");
    secured.tls_ca = missing;

    session_client session(secured);
    const auto connected = session.connect();

    REQUIRE_FALSE(connected.has_value());
    REQUIRE(connected.error().code == session_errc::tls_failed);
    REQUIRE(connected.error().message.contains(missing));
    REQUIRE_FALSE(session.is_connected());
}

TEST_CASE("a client key that cannot be opened is named in the failure",
    "[transport]") {
    /*
     * The three files are checked in turn, so the key is only reached when the
     * CA it sits beside is readable. A trust anchor that exists and names no
     * certificate is enough for that: nothing here completes a handshake.
     */
    const scratch_file present("ca.crt", "not a real certificate\n");
    const std::string missing = "/nonexistent/hedgr-tls/client.key";

    auto secured = options_for("ores.dev.brave_hopper");
    secured.tls_ca = present.path().string();
    secured.tls_key = missing;

    session_client session(secured);
    const auto connected = session.connect();

    REQUIRE_FALSE(connected.has_value());
    REQUIRE(connected.error().message.contains(missing));
    REQUIRE(connected.error().message.contains("key"));
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
