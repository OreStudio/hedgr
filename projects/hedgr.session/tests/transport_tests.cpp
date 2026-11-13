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
#include <map>
#include <set>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include "ores.nats/service/nats_connect_error.hpp"
#include "ores.nats/service/session_expired_error.hpp"

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

TEST_CASE("an expired session is named as a session expiry", "[transport]") {
    const ores::nats::service::session_expired_error expired(
        "Session has expired. Please log in again.");

    const auto error = translate_connect_error(expired);

    REQUIRE(error.code == session_errc::session_expired);
    REQUIRE(error.message == "the session has expired; sign in again");
    REQUIRE(error.detail == "Session has expired. Please log in again.");
}

TEST_CASE("a failure the transport cannot name is a protocol error",
    "[transport]") {
    const std::runtime_error other("NATS publish failed: Connection closed");

    const auto error = translate_connect_error(other);

    REQUIRE(error.code == session_errc::protocol_error);
    REQUIRE(error.detail == "NATS publish failed: Connection closed");
}

/**
 * @brief What a real deployment hands a chart and a flag, printed as it is.
 *
 * Hidden (`[.]`) and diagnostic on purpose: it asserts only that the reads
 * answered, and prints the shape of what came back, so a chart that draws a
 * sliver and a header with no flags can be told apart from a read that failed.
 * Run it by name, with the deployment's own values:
 *
 *     HEDGR_LIVE_URL=nats://localhost:20405 ... hedgr.session.tests "[.live-shape]"
 */
TEST_CASE("live shape: what a deployment says about a chart and its flags",
    "[.live-shape]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());
    std::printf("party=%s tenant=%s\n", signed_in->party_id.c_str(),
        signed_in->tenant_id.c_str());

    const auto series = session.market_series(5);
    if (!series) {
        std::printf("catalogue failed: %s (%s)\n",
            series.error().message.c_str(), series.error().detail.c_str());
    } else {
        std::printf("catalogue: %zu rows\n", series->size());
        for (const auto& row : *series)
            std::printf("  series %s label=%s asset=%s\n", row.id.c_str(),
                row.label.c_str(), row.asset_class.c_str());
    }

    if (series && !series->empty()) {
        const auto& first = series->front();
        std::printf("first series uri=%s party=%s provider=%s\n",
            first.oresmd_uri.c_str(), first.party_id.c_str(),
            first.provider_code.c_str());
        std::printf("  identity asset=%s scope=%s instrument=%s quote=%s "
                    "fields=%zu empty=%d\n",
            first.identity.asset.c_str(), first.identity.scope.c_str(),
            first.identity.instrument_type.c_str(),
            first.identity.quote_type.c_str(), first.identity.fields.size(),
            first.identity.empty() ? 1 : 0);
        for (const auto& field : first.identity.fields)
            std::printf("  field %s=%s\n", field.name.c_str(),
                field.text.c_str());

        const auto read = session.market_series_points(first.id);
        /*
         * The identity the catalogue carries names the party that owns the
         * series. The tick service scopes its reads by the session's own party,
         * so the two are tried apart as well as together: which of them the
         * service matches on is the question a refusal leaves open.
         */
        {
            auto no_party = first.identity;
            no_party.party_id.clear();
            const auto anon = session.market_series_points(first.id, no_party);
            std::printf("points with no party: %s\n",
                anon ? std::format("{} points", anon->points.size()).c_str()
                     : anon.error().message.c_str());

            auto session_party = first.identity;
            session_party.party_id = signed_in->party_id;
            const auto scoped =
                session.market_series_points(first.id, session_party);
            std::printf("points as the session's party: %s\n",
                scoped
                    ? std::format("{} points", scoped->points.size()).c_str()
                    : scoped.error().message.c_str());
        }
        if (!read) {
            std::printf("points failed: %s (%s)\n",
                read.error().message.c_str(), read.error().detail.c_str());
        } else {
            std::printf("points: %zu of %zu ticks, coordinates=%zu, "
                        "truncated=%d shortened=%d from=%lld to=%lld\n",
                read->points.size(), read->ticks, read->coordinates,
                read->truncated ? 1 : 0, read->shortened ? 1 : 0,
                static_cast<long long>(read->from_ns),
                static_cast<long long>(read->to_ns));
            if (!read->points.empty()) {
                const auto& oldest = read->points.front();
                const auto& newest = read->points.back();
                std::printf("  oldest=%lld value=%f newest=%lld value=%f\n",
                    static_cast<long long>(oldest.at_ns), oldest.value,
                    static_cast<long long>(newest.at_ns), newest.value);
                std::size_t zero = 0;
                std::size_t backwards = 0;
                for (std::size_t i = 1; i < read->points.size(); ++i) {
                    if (read->points[i].at_ns == 0)
                        ++zero;
                    if (read->points[i].at_ns < read->points[i - 1].at_ns)
                        ++backwards;
                }
                if (read->points.front().at_ns == 0)
                    ++zero;
                std::printf("  points at epoch zero=%zu out of order=%zu\n",
                    zero, backwards);
            }
        }
    }

    const auto images = session.currency_images();
    if (!images) {
        std::printf("currency images failed: %s (%s)\n",
            images.error().message.c_str(), images.error().detail.c_str());
    } else {
        std::printf("currency images: %zu\n", images->size());
        for (const auto& image : *images)
            std::printf("  currency=%s image=%s\n", image.currency.c_str(),
                image.image_id.c_str());
        for (std::size_t i = 0; i < images->size() && i < 4; ++i) {
            const auto& image = (*images)[i];
            const auto content = session.image_by_id(image.image_id);
            if (!content) {
                std::printf("  image %s failed: %s (%s)\n",
                    image.image_id.c_str(), content.error().message.c_str(),
                    content.error().detail.c_str());
                continue;
            }
            const auto text = std::string_view{
                reinterpret_cast<const char*>(content->data.data()),
                content->data.size()};
            std::printf("  %s %s %zu bytes closed=%d tail-hex",
                image.currency.c_str(), content->mime_type.c_str(),
                content->data.size(),
                text.ends_with("</svg>\n") ? 1 : 0);
            const auto first = content->data.size() > 12
                ? content->data.size() - 12 : content->data.size();
            for (std::size_t at = first; at < content->data.size(); ++at)
                std::printf(" %02x", static_cast<unsigned>(content->data[at]));
            std::printf("\n");
        }
    }
    std::fflush(stdout);
    session.disconnect();
}

/**
 * @brief The live check the plan cannot make from a short probe: a refresh.
 *
 * The actor arms its refresh at four fifths of the token's lifetime, so the
 * check that the *refresh itself* works needs either half an hour of waiting or
 * a call made the moment the sign-in lands. This is the second: the instance
 * issues a replacement whenever it is asked, so asking at once is the same code
 * path as asking at 1440 s, and it takes nine seconds rather than twenty-four
 * minutes.
 *
 * Hidden (`[.]`) for the same reason the runtime's probes are: it needs a
 * deployment, mTLS material and a credential. Run it by name:
 *
 *     HEDGR_LIVE_URL=nats://localhost:20005 \
 *     HEDGR_LIVE_PREFIX=ores.dev.prime_origin \
 *     HEDGR_LIVE_CA=<checkout>/build/keys/nats/ca.crt \
 *     HEDGR_LIVE_CERT=<checkout>/build/keys/nats/ores.shell.crt \
 *     HEDGR_LIVE_KEY=<checkout>/build/keys/nats/ores.shell.key \
 *     HEDGR_LIVE_PRINCIPAL=super_admin \
 *     HEDGR_LIVE_PASSWORD=... \
 *     hedgr.session.tests "[.live-refresh-now]"
 */
TEST_CASE("live refresh: a signed-in session renews its token at once",
    "[.live-refresh-now]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    if (!signed_in)
        std::printf("sign-in failed: %s [%s] (%s)\n",
            signed_in.error().message.c_str(),
            std::string{hedgr::session::transport::to_string(
                signed_in.error().code)}.c_str(),
            signed_in.error().detail.c_str());
    REQUIRE(signed_in.has_value());
    std::printf("signed in as '%s' with a token of %d s\n",
        signed_in->principal.c_str(), signed_in->access_lifetime_s);

    const auto refreshed = session.refresh();
    if (!refreshed)
        std::printf("refresh failed: %s [%s] (%s)\n",
            refreshed.error().message.c_str(),
            std::string{hedgr::session::transport::to_string(
                refreshed.error().code)}.c_str(),
            refreshed.error().detail.c_str());
    std::printf("refresh lifetime=%d\n", refreshed ? *refreshed : -1);
    std::fflush(stdout);

    CHECK(refreshed.has_value());
    session.disconnect();
}

/**
 * @brief Whether history reads the same for every provider of a series.
 *
 * Every FX spot is catalogued twice, once by the reference provider and once by
 * a data vendor, and only the vendor ticks. This reads a few series through the
 * catalogue's own identity, then the same identity with the provider dropped,
 * so the pair of answers says whether the provider is what makes a read name
 * one series instead of four.
 *
 * Hidden, with the live environment:
 *
 *     ... HEDGR_LIVE_* ... hedgr.session.tests "[.live-providers]"
 */
TEST_CASE("live providers: history names one series per provider",
    "[.live-providers]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    /*
     * A deployment that has just been reseeded answers the catalogue slowly
     * enough to time out the default, so this probe waits like an operator
     * would rather than declaring the read broken.
     */
    options.request_timeout = std::chrono::seconds{120};

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());

    /*
     * A handful is enough and all the deployment answers in time: the whole
     * catalogue is nearly five hundred rows and a single read of two hundred
     * times out. What the check needs is one row per provider, not every row.
     */
    const auto catalogue = session.market_series(6);
    if (!catalogue)
        std::printf("catalogue failed: %s (%s)\n",
            catalogue.error().message.c_str(),
            catalogue.error().detail.c_str());
    REQUIRE(catalogue.has_value());

    for (const auto& row : *catalogue) {
        std::printf("%s provider='%s' asset=%s\n", row.label.c_str(),
            row.provider_code.c_str(), row.asset_class.c_str());
        auto identity = row.identity;
        identity.party_id = signed_in->party_id;
        const auto named = session.market_series_points(row.id, identity);
        std::printf("  as catalogued -> %s\n",
            named ? std::format("{} points", named->points.size()).c_str()
                  : std::format("{} ({})", named.error().message,
                        named.error().detail).c_str());

        auto unspecified = identity;
        unspecified.provider_code.clear();
        const auto loose = session.market_series_points(row.id, unspecified);
        std::printf("  provider dropped -> %s\n",
            loose ? std::format("{} points", loose->points.size()).c_str()
                  : std::format("{} ({})", loose.error().message,
                        loose.error().detail).c_str());
    }
    std::fflush(stdout);
    session.disconnect();
}

/**
 * @brief What a curve whose ladder lives in its observations reads as.
 *
 * An interest-rate series can carry its whole ladder as observations of one
 * series, each naming its term. That is the source the term structure window
 * uses for such a series, because a tick reply names the coordinate rather than
 * the term and would answer with the index tenor alone. This asks for the
 * observations of every IR series the catalogue lists and prints the terms each
 * one names.
 *
 * Hidden, with the live environment:
 *
 *     ... HEDGR_LIVE_* ... hedgr.session.tests "[.live-ir-ladder]"
 */
TEST_CASE("live IR: the terms a curve's observations name",
    "[.live-ir-ladder]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());

    const auto series = session.market_series(500);
    REQUIRE(series.has_value());
    std::size_t seen = 0;
    for (const auto& row : *series) {
        if (row.asset_class.empty() || row.asset_class != "IR")
            continue;
        /*
         * The same series through both reads that could carry a ladder: the
         * observations, whose coordinates name the term, and the evolution,
         * whose shape declares its axes. Printing both is how the choice is
         * made from what the deployment states rather than from a guess.
         */
        const auto evolution = session.market_series_evolution(row.id,
            row.identity, 0, 0);
        if (!evolution) {
            std::printf("'%s' evolution failed: %s\n", row.label.c_str(),
                evolution.error().message.c_str());
            continue;
        }
        std::printf("'%s' evolution: fields ", row.label.c_str());
        for (const auto& field : evolution->fields)
            std::printf("'%s' ", field.c_str());
        std::printf("%zu nodes, %zu instants\n", evolution->nodes.size(),
            evolution->instants.size());
        for (std::size_t i = 0;
            i < evolution->nodes.size() && i < 12; ++i) {
            std::printf("  node:");
            for (const auto& label : evolution->nodes[i])
                std::printf(" %s", label.c_str());
            if (!evolution->instants.empty()
                && i < evolution->instants.back().values.size())
                std::printf(" = %.6f", evolution->instants.back().values[i]);
            std::printf("\n");
        }
        if (++seen == 3)
            break;
    }
    std::printf("read %zu IR series\n", seen);
    std::fflush(stdout);
    session.disconnect();
}

/**
 * @brief What a deployment publishes as curves, printed as it is.
 *
 * A curve is an object the deployment calibrates and publishes, not a set of
 * series a client assembles, so this asks for the party's published curves and
 * then for one curve's instants, and prints what came back: the name, the
 * instant, how many pillars, and the first few pillars with their values.
 *
 * Hidden, with the live environment:
 *
 *     ... HEDGR_LIVE_* ... hedgr.session.tests "[.live-curves]"
 */
TEST_CASE("live curves: what a deployment publishes for a party",
    "[.live-curves]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());
    std::printf("party=%s\n", signed_in->party_id.c_str());

    const auto sets = session.published_curves(20);
    if (!sets) {
        std::printf("published curves failed: %s (%s)\n",
            sets.error().message.c_str(), sets.error().detail.c_str());
        std::fflush(stdout);
        return;
    }
    std::printf("published curves: %zu\n", sets->size());
    for (std::size_t i = 0; i < sets->size() && i < 5; ++i) {
        const auto& set = (*sets)[i];
        std::printf("  curve='%s' series='%s' points=%u\n",
            set.label.c_str(), set.series_id.c_str(), set.points);
    }

    const hedgr::model::domain::published_curve* with_points = nullptr;
    for (const auto& set : *sets)
        if (!set.series_id.empty()) {
            with_points = &set;
            break;
        }
    if (with_points == nullptr) {
        std::printf("no curve names a series\n");
        std::fflush(stdout);
        return;
    }

    const auto instants =
        session.published_curve_sets(with_points->series_id, 5);
    if (!instants) {
        std::printf("curve sets failed: %s (%s)\n",
            instants.error().message.c_str(),
            instants.error().detail.c_str());
        std::fflush(stdout);
        return;
    }
    std::printf("curve sets for '%s': %zu\n", with_points->label.c_str(),
        instants->size());
    for (const auto& set : *instants) {
        std::printf("  as_of=%lld state='%s' points=%zu\n",
            static_cast<long long>(set.as_of_ns), set.state_code.c_str(),
            set.points.size());
        for (std::size_t i = 0; i < set.points.size() && i < 5; ++i)
            std::printf("    pillar='%s' value='%s' kind='%s'\n",
                set.points[i].pillar.c_str(),
                set.points[i].numeric
                    ? std::format("{}", set.points[i].value).c_str()
                    : "(not a number)",
                set.points[i].value_kind.c_str());
    }
    std::fflush(stdout);
    session.disconnect();
}

/**
 * @brief What a deployment publishes as FX volatility surfaces, printed as it is.
 *
 * A surface is calibrated from a blueprint and published as a grid: expiries on
 * one axis, strikes on the other, one volatility per cell. Nothing on the desk
 * draws it yet, so this asks what the deployment holds and prints the shape it
 * answers with - the blueprint, its recipe's grid, and the published grid's
 * axes and values - before a window is built on it.
 *
 * Hidden, with the live environment:
 *
 *     ... HEDGR_LIVE_* ... hedgr.session.tests "[.live-surfaces]"
 */
TEST_CASE("live surfaces: what a deployment publishes as a vol surface",
    "[.live-surfaces]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());
    std::printf("party=%s\n", signed_in->party_id.c_str());

    const auto blueprints = session.fx_vol_surface_blueprints(50);
    if (!blueprints) {
        std::printf("surface blueprints failed: %s (%s)\n",
            blueprints.error().message.c_str(),
            blueprints.error().detail.c_str());
        std::fflush(stdout);
        session.disconnect();
        return;
    }
    std::printf("surface blueprints: %zu\n", blueprints->size());
    for (const auto& blueprint : *blueprints) {
        std::printf("  '%s' id=%s pair=%s/%s model='%s'\n",
            blueprint.name.c_str(), blueprint.id.c_str(),
            blueprint.base_currency.c_str(),
            blueprint.quote_currency.c_str(), blueprint.smile_model.c_str());
        std::printf("    grid expiries='%s' strikes='%s'\n",
            blueprint.expiries.c_str(), blueprint.strikes.c_str());
    }
    if (blueprints->empty()) {
        std::fflush(stdout);
        session.disconnect();
        return;
    }

    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto& first = blueprints->front();
    const auto surface = session.published_fx_vol_surface(first.id, now_ns);
    if (!surface) {
        std::printf("published surface of '%s' failed: %s (%s)\n",
            first.name.c_str(), surface.error().message.c_str(),
            surface.error().detail.c_str());
        std::fflush(stdout);
        session.disconnect();
        return;
    }
    std::printf("surface of '%s' as_of=%lld publish=%s: %zu expiries x %zu "
                "strikes = %zu values\n",
        surface->name.c_str(), static_cast<long long>(surface->as_of_ns),
        surface->publish_id.c_str(), surface->expiries.size(),
        surface->strikes.size(), surface->values.size());
    for (const auto& expiry : surface->expiries)
        std::printf("  expiry='%s'\n", expiry.c_str());
    for (const auto& strike : surface->strikes)
        std::printf("  strike='%s'\n", strike.c_str());
    const auto strikes = surface->strikes.size();
    for (std::size_t row = 0; row < surface->expiries.size(); ++row) {
        std::printf("  %s:", surface->expiries[row].c_str());
        for (std::size_t column = 0;
            column < strikes && row * strikes + column < surface->values.size();
            ++column)
            std::printf(" %.6f", surface->values[row * strikes + column]);
        std::printf("\n");
    }
    std::fflush(stdout);
    session.disconnect();
}
