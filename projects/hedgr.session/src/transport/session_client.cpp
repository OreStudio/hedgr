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
#include "hedgr.session/transport/session_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include "ores.utility/rfl/reflectors.hpp" // IWYU pragma: keep.
#include "ores.iam.api/messaging/account_operations_protocol.hpp"
#include "ores.iam.api/messaging/bootstrap_protocol.hpp"
#include "ores.iam.api/messaging/login_protocol.hpp"
#include "ores.iam.api/messaging/password_policy_protocol.hpp"
#include "ores.iam.api/messaging/tenant_protocol.hpp"
#include "ores.iam.api/messaging/tenant_type_protocol.hpp"
#include "ores.marketdata.api/messaging/market_series_protocol.hpp"
#include "ores.marketdata.api/messaging/operations_protocol.hpp"
#include "ores.marketdata.api/messaging/provider_protocol.hpp"
#include "ores.refdata.api/messaging/crm_topology_config_protocol.hpp"
#include "ores.refdata.api/messaging/currency_pair_convention_protocol.hpp"
#include "ores.assets.api/messaging/image_protocol.hpp"
#include "ores.refdata.api/messaging/currency_protocol.hpp"
#include "ores.nats/domain/wire_codec.hpp"
#include "ores.nats/domain/wire_format.hpp"
#include "ores.nats/service/nats_connect_error.hpp"
#include "hedgr.session/market/series_label.hpp"
#include "hedgr.session/market/series_summary.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.session/transport/tick_span.hpp"
#include "ores.logging/make_logger.hpp"
#include "ores.nats/service/request_helpers.hpp"
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace {

using hedgr::session::transport::fail;

/**
 * @brief This client's logger.
 *
 * The transport is where a sign-in either works or does not, so this is the
 * place a failure has to leave a trace. Nothing secret is ever written: a
 * principal is named, a password and a token are not.
 */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.session.transport.session_client");
    return instance;
}
using hedgr::session::transport::session_errc;
using hedgr::session::transport::session_error;
using hedgr::session::transport::session_options;
using hedgr::session::transport::translate_connect_error;

/**
 * @brief The failure to report for an exception the transport threw.
 *
 * Every public method funnels its transport call through this, so no exception
 * reaches a caller. The text is kept: it is the only clue the wrapped client
 * leaves behind when it fails for a reason none of the codes above describes.
 */
[[nodiscard]] std::unexpected<session_error> transport_failure(const std::exception& error) {
    return std::unexpected<session_error>(translate_connect_error(error));
}

/**
 * @brief The failure to report for something that is not a std::exception.
 */
[[nodiscard]] std::unexpected<session_error> transport_failure() {
    return fail(session_errc::protocol_error,
        "the transport failed for a reason it did not name");
}

/**
 * @brief The two tenant types the deployment keeps for itself.
 *
 * The system tenant is the deployment's own administrator tenant and the
 * automation tenant is its test machinery. Neither is a tenant a person set up,
 * so the counts leave both out. The codes are the deployment's seed data, which
 * is why they are named here rather than derived: the rule is "not these two",
 * and the set of everything else is read from the deployment.
 */
constexpr std::string_view system_tenant_type = "system";
constexpr std::string_view automation_tenant_type = "automation";

/**
 * @brief Asks one deployment-relative subject and decodes its answer.
 *
 * The two reads that come before a sign-in differ only in which pair of
 * messages they name, so the encoding, the unauthenticated request and the
 * decoding are written once. The body is encoded here rather than by the
 * transport's own helper, because that helper encodes with the process-wide
 * codec while this session's format is the one the deployment is listening in:
 * a session holds one connection to one instance, so the format on its options
 * is the only answer that is right for it.
 *
 * @tparam Request the generated request type, which names its own subject.
 * @tparam Response the generated response type the deployment answers with.
 * @pre the session is connected.
 */
template <typename Request, typename Response>
[[nodiscard]] hedgr::session::transport::result<Response>
anonymous_read(const hedgr::session::transport::session_client& session,
    const hedgr::session::transport::session_options& options) {
    const auto format = ores::nats::parse_wire_format(options.wire_format);
    if (!format) {
        return fail(session_errc::protocol_error,
            std::format("the wire format '{}' is not one this client understands",
                options.wire_format));
    }

    const ores::nats::wire_codec codec(*format);
    const auto encoded = codec.encode(Request{});
    const std::string body(reinterpret_cast<const char*>(encoded.data()),
        encoded.size());

    /*
     * The unauthenticated request path and nothing else: a caller reaches
     * either of these before it has a credential, so neither can require one.
     */
    const auto reply = session.request(Request::nats_subject, body);
    if (!reply)
        return std::unexpected(reply.error());

    const auto bytes = std::as_bytes(
        std::span<const char>(reply->data(), reply->size()));
    const auto decoded = codec.decode<Response>(bytes);
    if (!decoded) {
        return fail(session_errc::protocol_error,
            std::format("the answer to {} could not be decoded: {}",
                Request::nats_subject, decoded.error().what()));
    }
    return *decoded;
}

/**
 * @brief The failure to report when a TLS file the options name cannot be read.
 *
 * A mistyped path and an unreachable broker both end as a connection that never
 * opened, and the wrapped client reports them the same way, so neither the
 * reader nor this component could tell which of the two had happened -- and the
 * two need very different fixes. Opening each file here, before the client is
 * handed it, turns one of them into a sentence that names the file.
 *
 * Only the files the options actually name are checked, and only when TLS is
 * on: a certificate beside no CA is ignored by @c uses_tls, and reporting it as
 * missing would be complaining about a file that would not have been read.
 */
[[nodiscard]] std::optional<session_error>
unreadable_tls_file(const session_options& options) {
    if (!options.uses_tls())
        return std::nullopt;

    const std::array<std::pair<std::string_view, std::string_view>, 3> files{{
        {"CA certificate", options.tls_ca},
        {"client certificate", options.tls_cert},
        {"client key", options.tls_key},
    }};

    for (const auto& [role, path] : files) {
        if (path.empty())
            continue;
        std::ifstream input{std::filesystem::path{path}};
        if (input)
            continue;

        return session_error{
            .code = session_errc::tls_failed,
            .message = std::format("the {} {} cannot be opened",
                role, path),
            .detail = std::format(
                "the path named for the {} does not name a file that can be "
                "opened for reading",
                role),
        };
    }
    return std::nullopt;
}

/**
 * @brief The failure to report when opening a connection fails.
 *
 * A connection that never opened is reported as unreachable when the wrapped
 * client does not say why: whatever the cause -- a missing certificate, a
 * broker that refuses -- nothing answered, and a protocol error would tell the
 * reader the instance replied, which it did not.
 */
[[nodiscard]] std::unexpected<session_error>
connect_failure(const std::exception& error) {
    if (dynamic_cast<const ores::nats::service::nats_connect_error*>(&error) != nullptr)
        return std::unexpected<session_error>(translate_connect_error(error));

    return fail(session_errc::unreachable, error.what());
}

/**
 * @brief The failure to report for something that is not a std::exception.
 */
[[nodiscard]] std::unexpected<session_error> connect_failure() {
    return fail(session_errc::unreachable,
        "the connection failed for a reason it did not name");
}

}

namespace hedgr::session::transport {

session_client::session_client(session_options options)
    : options_(std::move(options)) {
}

session_client::~session_client() {
    disconnect();
}

ores::nats::service::nats_client& session_client::client() const {
    return const_cast<ores::nats::service::nats_client&>(nats_);
}

result<void> session_client::connect() {
    /*
     * Already open is the state the caller asked for, so a retry converges
     * rather than opening a second connection on top of the first.
     */
    if (is_connected())
        return {};

    const auto format = ores::nats::parse_wire_format(options_.wire_format);
    if (!format) {
        /*
         * Refused rather than defaulted: a session that encodes msgpack while
         * the deployment decodes json answers every request with a decode
         * failure, which reads as the instance being broken.
         */
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Refusing to connect: wire format '" << options_.wire_format
            << "' is not understood";
        return fail(session_errc::protocol_error,
            std::format("the wire format '{}' is not one this client understands",
                options_.wire_format));
    }

    ores::nats::config::nats_options opts;
    opts.url = options_.nats_url;
    opts.subject_prefix = options_.subject_prefix;
    opts.format = *format;
    if (options_.uses_tls()) {
        /*
         * Checked before the client is given them, so that a path which names
         * nothing is reported as the path it is rather than as a broker that
         * did not answer.
         */
        if (auto unreadable = unreadable_tls_file(options_)) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Refusing to connect to " << options_.nats_url
                << ": " << unreadable->message;
            return std::unexpected(std::move(*unreadable));
        }

        opts.tls_ca_cert = options_.tls_ca;
        opts.tls_client_cert = options_.tls_cert;
        opts.tls_client_key = options_.tls_key;
    }

    /*
     * connect_timeout has no counterpart in the wrapped client's options, so
     * it is recorded for the caller and not yet honoured on the wire.
     */
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Connecting to " << options_.nats_url << " (prefix '"
        << options_.subject_prefix << "', wire " << options_.wire_format
        << ", tls " << (options_.uses_tls() ? "yes" : "no") << ")";

    try {
        nats_.connect(std::move(opts));
    } catch (const std::exception& e) {
        connected_ = false;
        auto failure = connect_failure(e);
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Could not connect to " << options_.nats_url << ": "
            << failure.error().message << " ["
            << to_string(failure.error().code) << "]";
        return failure;
    } catch (...) {
        connected_ = false;
        auto failure = connect_failure();
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Could not connect to " << options_.nats_url
            << " for an unnamed reason";
        return failure;
    }

    connected_ = true;
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Connected to " << options_.nats_url;
    return {};
}

void session_client::disconnect() {
    /*
     * The credential goes first: a connection dropped while a token is still
     * held is a session that could be resumed by accident.
     */
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Disconnecting from " << options_.nats_url;
    try {
        nats_.clear_auth();
        nats_.disconnect();
    } catch (const std::exception&) {
        /* Teardown reports nothing: the caller asked for the session to be
         * gone, and it is gone from here either way. */
    } catch (...) {
    }

    connected_ = false;
    account_ = session_account{};
}

bool session_client::is_connected() const noexcept {
    return connected_ && nats_.is_connected();
}

bool session_client::is_authenticated() const noexcept {
    return !account_.token.empty();
}

result<session_account> session_client::authenticate(std::string_view principal,
    std::string_view password) {

    if (!is_connected()) {
        return fail(session_errc::closed,
            "the session must be connected before signing in");
    }

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Signing in as '" << principal << "' on '"
        << options_.subject_prefix << "'";

    using ores::iam::messaging::login_request;
    using ores::iam::messaging::login_response;

    try {
        const login_request request{
            .principal = std::string(principal),
            .password = std::string(password)};

        const auto reply = ores::nats::service::request_and_decode<login_response>(
            nats_, login_request::nats_subject, request);
        if (!reply) {
            return fail(session_errc::protocol_error,
                "the sign-in answer could not be decoded");
        }

        const auto& answer = *reply;
        if (!answer.success) {
            /*
             * The instance's own words, because it is the only party that
             * knows why it refused -- a locked account and a wrong password
             * are not the same thing to the person reading this.
             */
            const std::string message = answer.message.empty()
                ? answer.error_message : answer.message;
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Sign-in refused for '" << principal << "' on '"
                << options_.subject_prefix << "': "
                << (message.empty() ? std::string{"no reason given"} : message)
                << " [" << (answer.error_code.empty()
                        ? std::string{"no code"} : answer.error_code)
                << "]";
            return std::unexpected(session_error{
                .code = session_errc::authentication_rejected,
                .message = message.empty()
                    ? std::string("the instance rejected these credentials")
                    : message,
                .detail = answer.error_code.empty()
                    ? answer.error_message
                    : std::format("{}: {}", answer.error_code,
                        answer.error_message)});
        }

        ores::nats::service::nats_client::login_info info;
        info.jwt = answer.token;
        info.username = answer.username;
        info.account_id = answer.account_id;
        info.tenant_id = answer.tenant_id;
        info.tenant_name = answer.tenant_name;
        info.default_party_id = answer.default_party_id.empty()
            ? answer.selected_party_id : answer.default_party_id;
        nats_.set_auth(std::move(info));

        /*
         * The party the answer scoped the token to, by name. The list the
         * answer carries is the only place the name is stated, so it is read
         * here rather than looked up later.
         */
        std::string party_name;
        std::vector<transport::party_choice> party_choices;
        for (const auto& party : answer.available_parties) {
            if (party.id == answer.selected_party_id)
                party_name = party.name;
            party_choices.push_back(transport::party_choice{
                .id = party.id, .name = party.name});
        }

        account_ = session_account{
            .token = answer.token,
            .tenant_id = answer.tenant_id,
            .tenant_name = answer.tenant_name,
            .party_id = answer.selected_party_id,
            .party_name = party_name,
            .principal = answer.username.empty()
                ? std::string(principal) : answer.username,
            .access_lifetime_s = answer.access_lifetime_s > 0
                ? answer.access_lifetime_s : 1800};

        /*
         * A session with no party and parties to choose from has to choose one
         * before it can act: the deployment's later reads are scoped by the
         * party, and a single-use token that only select_party may present
         * would refuse them. The account's default is the account's own
         * preference, so it wins; otherwise the first party is the only
         * deterministic choice left.
         */
        if (account_.party_id.empty() && !answer.available_parties.empty()) {
            const std::string chosen = !answer.default_party_id.empty()
                ? answer.default_party_id
                : answer.available_parties.front().id;
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Session has no party; selecting '" << chosen << "'";
            if (auto selected = select_party(chosen); !selected)
                return std::unexpected(selected.error());
        }

        /*
         * The parties this login may act as, read every time rather than only
         * when the answer left the name out. The sign-in answer names the party
         * it scoped the token to, but the picker in the series browser needs
         * every party the account may choose, and IAM's own list is the only
         * authority for that. A failed read leaves the answer's list in place.
         */
        {
            using ores::iam::messaging::get_my_parties_request;
            using ores::iam::messaging::get_my_parties_response;

            try {
                const auto parties =
                    ores::nats::service::authenticated_request_and_decode<
                        get_my_parties_response>(client(),
                        get_my_parties_request::nats_subject,
                        get_my_parties_request{}, options_.request_timeout);
                if (!parties) {
                    BOOST_LOG_SEV(lg(), ores::logging::warn)
                        << "The parties of this session's account could not be "
                           "decoded";
                } else if (parties->result.outcome
                    != ores::utility::domain::outcome::ok) {
                    BOOST_LOG_SEV(lg(), ores::logging::warn)
                        << "The deployment refused the parties read: "
                        << parties->result.message;
                } else if (account_.party_id.empty()
                    && parties->parties.size() == 1) {
                    /*
                     * One party and no selection is not a choice: the session
                     * has exactly one place it can act, and leaving it
                     * unscoped would make every later read refuse.
                     */
                    if (auto selected = select_party(
                            parties->parties.front().party_id);
                        !selected) {
                        BOOST_LOG_SEV(lg(), ores::logging::warn)
                            << "Could not select the only party of this "
                               "account: " << selected.error().message;
                    }
                } else {
                    for (const auto& party : parties->parties) {
                        if (party.party_id == account_.party_id)
                            account_.party_name = party.name;
                        const bool known = std::any_of(party_choices.begin(),
                            party_choices.end(), [&party](const auto& choice) {
                                return choice.id == party.party_id;
                            });
                        if (!known) {
                            party_choices.push_back(transport::party_choice{
                                .id = party.party_id, .name = party.name});
                        }
                    }
                }
            } catch (const std::exception& e) {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The parties of this session could not be read: "
                    << e.what();
            } catch (...) {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "The parties of this session could not be read";
            }
        }

        account_.parties = std::move(party_choices);

        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Signed in as '" << account_.principal << "' on '"
            << options_.subject_prefix << "' as tenant '"
            << account_.tenant_name << "' (" << account_.tenant_id << ")"
            << (account_.party_name.empty()
                    ? std::string{" with no party"}
                    : std::format(" in party '{}'", account_.party_name));
        return account_;
    } catch (const std::exception& e) {
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Sign-in for '" << principal << "' failed in the transport: "
            << e.what();
        return transport_failure(e);
    } catch (...) {
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Sign-in for '" << principal << "' failed for an unnamed reason";
        return transport_failure();
    }
}

ores::nats::service::subscription session_client::subscribe(
    std::string_view subject,
    ores::nats::message_handler handler) const {
    return client().transport().subscribe(subject, std::move(handler));
}

result<std::vector<session_client::crm_rate>>
session_client::crm_rates(std::string_view party_id,
    std::string_view crm_name, bool reciprocal) const {
    if (!is_connected() || !is_authenticated())
        return fail(session_errc::not_authenticated,
            "a CRM read needs a signed-in session");

    using ores::marketdata::messaging::get_crm_rates_request;
    using ores::marketdata::messaging::get_crm_rates_response;
    try {
        get_crm_rates_request request{};
        request.party_id = std::string{party_id};
        request.crm_name = std::string{crm_name};
        request.reciprocal = reciprocal;
        const auto answer =
            ores::nats::service::authenticated_request_and_decode<
                get_crm_rates_response>(client(),
                get_crm_rates_request::nats_subject, request,
                options_.request_timeout);
        if (!answer)
            return fail(session_errc::protocol_error,
                "the CRM answer could not be decoded");
        if (!answer->success)
            return fail(session_errc::protocol_error,
                answer->message.empty()
                    ? std::string("the deployment refused the CRM read")
                    : answer->message);

        std::vector<crm_rate> result;
        result.reserve(answer->rates.size());
        for (const auto& item : answer->rates) {
            result.push_back(crm_rate{
                .crm_name = item.crm_name,
                .base_currency_code = item.base_currency_code,
                .quote_currency_code = item.quote_currency_code,
                .rate = item.rate,
                .status = item.status,
                .as_of = item.as_of,
                .reciprocal = item.reciprocal,
                .delta_pct = item.delta_pct});
        }
        return result;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<std::vector<std::string>>
session_client::crm_names(std::string_view party_id) const {
    if (!is_connected() || !is_authenticated())
        return fail(session_errc::not_authenticated,
            "a CRM name read needs a signed-in session");

    using ores::refdata::messaging::list_crm_topology_configs_request;
    using ores::refdata::messaging::list_crm_topology_configs_response;
    try {
        std::vector<std::string> result;
        std::uint32_t offset = 0;
        while (true) {
            list_crm_topology_configs_request request{};
            request.offset = offset;
            request.limit = 500;
            const auto answer =
                ores::nats::service::authenticated_request_and_decode<
                    list_crm_topology_configs_response>(client(),
                    list_crm_topology_configs_request::nats_subject, request,
                    options_.request_timeout);
            if (!answer)
                return fail(session_errc::protocol_error,
                    "the CRM name answer could not be decoded");
            if (answer->result.outcome != ores::utility::domain::outcome::ok)
                return fail(session_errc::protocol_error,
                    answer->result.message);
            for (const auto& config : answer->crm_topology_configs) {
                if (!config.enabled
                    || boost::uuids::to_string(config.party_id)
                        != party_id)
                    continue;
                result.push_back(config.name);
            }
            offset += static_cast<std::uint32_t>(
                answer->crm_topology_configs.size());
            if (answer->crm_topology_configs.empty() || offset >= answer->total)
                break;
        }
        return result;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<std::vector<session_client::pair_convention>>
session_client::currency_pair_conventions() const {
    if (!is_connected() || !is_authenticated())
        return fail(session_errc::not_authenticated,
            "a pair convention read needs a signed-in session");

    using ores::refdata::messaging::list_currency_pair_conventions_request;
    using ores::refdata::messaging::list_currency_pair_conventions_response;
    try {
        std::vector<pair_convention> result;
        std::uint32_t offset = 0;
        while (true) {
            list_currency_pair_conventions_request request{};
            request.offset = offset;
            request.limit = 500;
            const auto answer =
                ores::nats::service::authenticated_request_and_decode<
                    list_currency_pair_conventions_response>(client(),
                    list_currency_pair_conventions_request::nats_subject,
                    request, options_.request_timeout);
            if (!answer)
                return fail(session_errc::protocol_error,
                    "the pair convention answer could not be decoded");
            if (answer->result.outcome != ores::utility::domain::outcome::ok)
                return fail(session_errc::protocol_error,
                    answer->result.message);
            for (const auto& convention : answer->conventions) {
                result.push_back(pair_convention{
                    .pair_code = convention.pair_code,
                    .decimal_places = convention.decimal_places,
                    .pip_factor = convention.pip_factor,
                    .tick_size = convention.tick_size});
            }
            offset += static_cast<std::uint32_t>(answer->conventions.size());
            if (answer->conventions.empty() || offset >= answer->total)
                break;
        }
        return result;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<std::vector<session_client::currency_image>>
session_client::currency_images() const {
    if (!is_connected() || !is_authenticated())
        return fail(session_errc::not_authenticated,
            "a currency image read needs a signed-in session");

    using ores::refdata::messaging::list_currencies_request;
    using ores::refdata::messaging::list_currencies_response;
    try {
        std::vector<currency_image> result;
        std::uint32_t offset = 0;
        while (true) {
            list_currencies_request request{};
            request.offset = offset;
            request.limit = 500;
            const auto answer =
                ores::nats::service::authenticated_request_and_decode<
                    list_currencies_response>(client(),
                    list_currencies_request::nats_subject, request,
                    options_.request_timeout);
            if (!answer)
                return fail(session_errc::protocol_error,
                    "the currencies answer could not be decoded");
            if (answer->result.outcome != ores::utility::domain::outcome::ok)
                return fail(session_errc::protocol_error,
                    answer->result.message);

            for (const auto& currency : answer->currencies) {
                if (!currency.image_id)
                    continue;
                result.push_back(currency_image{
                    .currency = currency.iso_code,
                    .image_id = boost::uuids::to_string(*currency.image_id)});
            }
            offset += static_cast<std::uint32_t>(answer->currencies.size());
            if (answer->currencies.empty() || offset >= answer->total)
                break;
        }
        return result;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<session_client::image_content>
session_client::image_by_id(std::string_view image_id) const {
    if (!is_connected() || !is_authenticated())
        return fail(session_errc::not_authenticated,
            "an image read needs a signed-in session");

    using ores::assets::messaging::images_filter;
    using ores::assets::messaging::list_images_request;
    using ores::assets::messaging::list_images_response;
    try {
        list_images_request request{};
        request.limit = 1;
        request.filter = images_filter{};
        request.filter->id_one_of = std::vector<boost::uuids::uuid>{
            boost::lexical_cast<boost::uuids::uuid>(image_id)};
        const auto answer =
            ores::nats::service::authenticated_request_and_decode<
                list_images_response>(client(),
                list_images_request::nats_subject, request,
                options_.request_timeout);
        if (!answer)
            return fail(session_errc::protocol_error,
                "the image answer could not be decoded");
        if (answer->result.outcome != ores::utility::domain::outcome::ok)
            return fail(session_errc::protocol_error, answer->result.message);
        if (answer->images.empty())
            return fail(session_errc::protocol_error,
                "the deployment holds no image with that identifier");
        return image_content{.mime_type = answer->images.front().mime_type,
            .data = answer->images.front().data};
    } catch (const boost::bad_lexical_cast&) {
        return fail(session_errc::protocol_error,
            "the image identifier is not a UUID");
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<int> session_client::refresh() {
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a token can only be refreshed by a signed-in session");
    }

    using ores::iam::messaging::refresh_request;
    using ores::iam::messaging::refresh_response;

    try {
        const auto reply =
            ores::nats::service::authenticated_request_and_decode<
                refresh_response>(client(), refresh_request::nats_subject,
                refresh_request{}, options_.request_timeout);
        if (!reply) {
            return fail(session_errc::protocol_error,
                "the token refresh answer could not be decoded");
        }

        const auto& answer = *reply;
        if (!answer.success) {
            return std::unexpected(session_error{
                .code = session_errc::authentication_rejected,
                .message = answer.message.empty()
                    ? std::string("the instance refused to refresh the session")
                    : answer.message,
                .detail = std::format("{} answered success=false",
                    refresh_request::nats_subject)});
        }

        auto info = nats_.auth();
        info.jwt = answer.token;
        nats_.set_auth(std::move(info));
        account_.token = answer.token;
        if (answer.access_lifetime_s > 0)
            account_.access_lifetime_s = answer.access_lifetime_s;

        return account_.access_lifetime_s;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

template <typename Request>
result<void> session_client::choose_party(std::string_view party_id) {
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a party can only be chosen by a signed-in session");
    }

    using response_type = typename Request::response_type;

    try {
        const Request request{.party_id = std::string(party_id)};

        const auto reply =
            ores::nats::service::authenticated_request_and_decode<response_type>(
                nats_, Request::nats_subject, request,
                options_.request_timeout);
        if (!reply) {
            return fail(session_errc::protocol_error,
                "the party selection answer could not be decoded");
        }

        const auto& answer = *reply;
        if (!answer.success) {
            return std::unexpected(session_error{
                .code = session_errc::authentication_rejected,
                .message = answer.message.empty()
                    ? std::string("the instance refused this party")
                    : answer.message,
                .detail = std::format("{} answered success=false",
                    Request::nats_subject)});
        }

        /*
         * The selection hands back a fresh token scoped to the party, so the
         * credential the session holds is replaced rather than kept.
         */
        auto info = nats_.auth();
        info.jwt = answer.token;
        info.default_party_id = std::string(party_id);
        nats_.set_auth(std::move(info));

        account_.token = answer.token;
        account_.party_id = std::string(party_id);
        account_.party_name = answer.party_name;
        account_.access_lifetime_s = answer.access_lifetime_s > 0
            ? answer.access_lifetime_s : account_.access_lifetime_s;
        if (!answer.tenant_name.empty())
            account_.tenant_name = answer.tenant_name;
        if (!answer.username.empty())
            account_.principal = answer.username;

        return {};
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<void> session_client::select_party(std::string_view party_id) {
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Selecting party '" << party_id << "' on '"
        << options_.subject_prefix << "'";
    return choose_party<ores::iam::messaging::select_party_request>(party_id);
}

result<void> session_client::switch_party(std::string_view party_id) {
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Switching to party '" << party_id << "' on '"
        << options_.subject_prefix << "'";
    return choose_party<ores::iam::messaging::switch_party_request>(party_id);
}

result<void> session_client::logout() {
    if (!is_authenticated()) {
        /*
         * Nothing to sign out of. The state the caller asked for already
         * holds, so this is a success rather than an error: a sign-out is a
         * state, and repeating it must not fail.
         */
        nats_.clear_auth();
        account_ = session_account{};
        return {};
    }

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Signing out of '" << options_.subject_prefix << "'";

    using ores::iam::messaging::logout_request;
    using ores::iam::messaging::logout_response;

    const auto outcome = [&]() -> result<void> {
        try {
            const auto reply =
                ores::nats::service::authenticated_request_and_decode<logout_response>(
                    nats_, logout_request::nats_subject, logout_request{},
                    options_.request_timeout);
            if (!reply) {
                return fail(session_errc::protocol_error,
                    "the sign-out answer could not be decoded");
            }
            if (!reply->success) {
                return std::unexpected(session_error{
                    .code = session_errc::authentication_rejected,
                    .message = reply->message.empty()
                        ? std::string("the instance refused the sign-out")
                        : reply->message,
                    .detail = std::format("{} answered success=false",
                        logout_request::nats_subject)});
            }
            return {};
        } catch (const std::exception& e) {
            return transport_failure(e);
        } catch (...) {
            return transport_failure();
        }
    }();

    /*
     * The credential is dropped whatever the instance answered: a caller
     * that asked to sign out must not still be holding a token afterwards,
     * and a call that could not be delivered must not leave the session
     * looking signed in.
     */
    nats_.clear_auth();
    account_ = session_account{};

    return outcome;
}

result<std::string> session_client::request(std::string_view relative_subject,
    std::string_view json) const {

    if (!is_connected()) {
        return fail(session_errc::closed,
            "a request needs an open connection to the instance");
    }

    try {
        const auto reply = client().request(relative_subject, json);
        return std::string(reinterpret_cast<const char*>(reply.data.data()),
            reply.data.size());
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<std::string>
session_client::authenticated_request(std::string_view relative_subject,
    std::string_view json) const {

    if (!is_connected()) {
        return fail(session_errc::closed,
            "a request needs an open connection to the instance");
    }

    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "an authenticated request needs a signed-in session");
    }

    try {
        const auto reply = client().authenticated_request(relative_subject, json,
            options_.request_timeout);
        return std::string(reinterpret_cast<const char*>(reply.data.data()),
            reply.data.size());
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<bootstrap_status> session_client::bootstrap_status() const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a request needs an open connection to the instance");
    }

    /*
     * Asking whether a deployment has been set up cannot require having been
     * set up, so this is one of the two reads that go out without a token.
     */
    using ores::iam::messaging::bootstrap_status_request;
    using ores::iam::messaging::bootstrap_status_response;

    const auto answer = anonymous_read<bootstrap_status_request,
        bootstrap_status_response>(*this, options_);
    if (!answer)
        return std::unexpected(answer.error());

    /*
     * Qualified because the method shares its name with the type it returns,
     * and an unqualified use inside the method finds the method.
     */
    return transport::bootstrap_status{
        .is_in_bootstrap_mode = answer->is_in_bootstrap_mode,
        .has_tenant = answer->has_tenant,
        .message = answer->message,
        .version = answer->version};
}

result<password_policy> session_client::password_policy() const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a request needs an open connection to the instance");
    }

    /*
     * The second read that needs no credential, and for the same reason as the
     * bootstrap one: the screen that shows the rules is the one a person signs
     * in on, so there is no session to present when it is drawn.
     */
    using ores::iam::messaging::get_password_policy_request;
    using ores::iam::messaging::get_password_policy_response;

    const auto answer = anonymous_read<get_password_policy_request,
        get_password_policy_response>(*this, options_);
    if (!answer)
        return std::unexpected(answer.error());

    return transport::password_policy{
        .success = answer->success,
        .message = answer->message,
        .min_length = answer->min_length,
        .require_uppercase = answer->require_uppercase,
        .require_lowercase = answer->require_lowercase,
        .require_digit = answer->require_digit,
        .require_special = answer->require_special,
        .special_chars = answer->special_chars};
}

result<void> session_client::resume(std::string_view token,
    std::string_view principal) {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a token needs an open connection to the instance");
    }
    if (token.empty()) {
        return fail(session_errc::not_authenticated,
            "a session cannot resume with an empty token");
    }

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Resuming a session on '" << options_.subject_prefix
        << "' as '" << principal << "'";

    ores::nats::service::nats_client::login_info info;
    info.jwt = std::string(token);
    info.username = std::string(principal);
    nats_.set_auth(std::move(info));

    account_ = session_account{.token = std::string(token),
        .principal = std::string(principal)};
    return {};
}

result<deployment_read> session_client::deployment() const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a deployment read needs an open connection to the instance");
    }
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a deployment read needs a signed-in session");
    }

    using ores::iam::messaging::list_tenant_types_request;
    using ores::iam::messaging::list_tenant_types_response;
    using ores::iam::messaging::list_tenants_request;
    using ores::iam::messaging::list_tenants_response;
    using ores::iam::messaging::tenants_filter;

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Reading the deployment on '" << options_.subject_prefix << "'";

    try {
        /*
         * The set of types a person's tenants may have, read rather than
         * assumed. A deployment that adds a type gets it counted without a
         * change here, and the two machinery types stay excluded by name.
         */
        list_tenant_types_request types_request{};
        types_request.limit = 100;
        const auto types_answer =
            ores::nats::service::authenticated_request_and_decode<
                list_tenant_types_response>(client(),
                list_tenant_types_request::nats_subject, types_request,
                options_.request_timeout);
        if (!types_answer) {
            return fail(session_errc::protocol_error,
                "the tenant types answer could not be decoded");
        }
        if (types_answer->result.outcome != ores::utility::domain::outcome::ok) {
            return fail(session_errc::protocol_error,
                types_answer->result.message.empty()
                    ? std::string("the deployment refused the tenant types read")
                    : types_answer->result.message);
        }

        std::vector<std::string> shown;
        shown.reserve(types_answer->types.size());
        for (const auto& type : types_answer->types) {
            if (type.type == system_tenant_type
                || type.type == automation_tenant_type) {
                continue;
            }
            shown.push_back(type.type);
        }

        /*
         * One count is one list read that asks for its total and one row, so
         * the deployment answers from its own index rather than from a page
         * nobody reads. The status and the type are both filters rather than
         * fields on the rows, because the total is the deployment's answer and
         * filtering it here would count what this client happened to fetch.
         */
        const auto count = [&](std::string status,
                               std::string type) -> result<std::uint64_t> {
            list_tenants_request request{};
            request.limit = 1;
            tenants_filter filter{};
            if (!type.empty())
                filter.type = std::move(type);
            if (!status.empty())
                filter.status = std::move(status);
            filter.type_one_of = shown;
            request.filter = std::move(filter);

            const auto answer =
                ores::nats::service::authenticated_request_and_decode<
                    list_tenants_response>(client(),
                    list_tenants_request::nats_subject, request,
                    options_.request_timeout);
            if (!answer) {
                return fail(session_errc::protocol_error,
                    "the tenant count answer could not be decoded");
            }
            if (answer->result.outcome
                != ores::utility::domain::outcome::ok) {
                return fail(session_errc::protocol_error,
                    answer->result.message.empty()
                        ? std::string("the deployment refused a tenant count")
                        : answer->result.message);
            }
            return answer->total;
        };

        const auto total = count({}, {});
        if (!total)
            return std::unexpected(total.error());
        const auto in_service = count("active", {});
        if (!in_service)
            return std::unexpected(in_service.error());
        const auto on_evaluation = count({}, "evaluation");
        if (!on_evaluation)
            return std::unexpected(on_evaluation.error());
        const auto setting_up = count("bootstrapping", {});
        if (!setting_up)
            return std::unexpected(setting_up.error());
        const auto attention = count("suspended", {});
        if (!attention)
            return std::unexpected(attention.error());

        /*
         * The roster is the same filter with rows asked for. Fifty is a screen,
         * not a page: the command centre's tenant list is the first fifty and
         * says so, and a full paged roster is a later screen with its own
         * controls.
         */
        list_tenants_request page_request{};
        page_request.limit = 50;
        tenants_filter page_filter{};
        page_filter.type_one_of = shown;
        page_request.filter = std::move(page_filter);
        const auto page =
            ores::nats::service::authenticated_request_and_decode<
                list_tenants_response>(client(),
                list_tenants_request::nats_subject, page_request,
                options_.request_timeout);
        if (!page) {
            return fail(session_errc::protocol_error,
                "the tenant roster answer could not be decoded");
        }
        if (page->result.outcome != ores::utility::domain::outcome::ok) {
            return fail(session_errc::protocol_error,
                page->result.message.empty()
                    ? std::string("the deployment refused the tenant roster")
                    : page->result.message);
        }

        deployment_read read;
        read.tenant_total = *total;
        read.in_service = *in_service;
        read.on_evaluation = *on_evaluation;
        read.setting_up = *setting_up;
        read.needing_attention = *attention;
        read.tenants.reserve(page->tenants.size());
        for (const auto& row : page->tenants) {
            read.tenants.push_back(tenant_summary{.id =
                                                      boost::uuids::to_string(row.id),
                .name = row.name,
                .status = row.status,
                .type = row.type});
        }
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Deployment '" << options_.subject_prefix
            << "': " << read.tenant_total << " tenants (" << read.in_service
            << " in service, " << read.on_evaluation << " on evaluation, "
            << read.setting_up << " setting up, " << read.needing_attention
            << " needing attention)";
        return read;
    } catch (const std::exception& e) {
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Reading the deployment on '" << options_.subject_prefix
            << "' failed: " << e.what();
        return transport_failure(e);
    } catch (...) {
        BOOST_LOG_SEV(lg(), ores::logging::error)
            << "Reading the deployment on '" << options_.subject_prefix
            << "' failed for an unnamed reason";
        return transport_failure();
    }
}

result<std::vector<market_series_summary>>
session_client::market_series(std::uint32_t limit) const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a market series read needs an open connection to the instance");
    }
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a market series read needs a signed-in session");
    }

    using ores::marketdata::messaging::list_market_series_request;
    using ores::marketdata::messaging::list_market_series_response;

    try {
        list_market_series_request request{};
        request.limit = limit;
        /*
         * The deployment states which fields a list may be ordered by, and the
         * series URI is not one of them. The read is sorted here instead, so
         * the order a screen shows does not depend on the wire's vocabulary.
         */

        const auto answer =
            ores::nats::service::authenticated_request_and_decode<
                list_market_series_response>(client(),
                list_market_series_request::nats_subject, request,
                options_.request_timeout);
        if (!answer) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The market series answer could not be decoded: "
                << answer.error().what();
            return fail(session_errc::protocol_error,
                "the market series answer could not be decoded");
        }
        if (answer->result.outcome != ores::utility::domain::outcome::ok) {
            return fail(session_errc::protocol_error,
                answer->result.message.empty()
                    ? std::string("the deployment refused the market series")
                    : answer->result.message);
        }

        std::vector<market_series_summary> rows;
        rows.reserve(answer->market_series.size());
        for (const auto& row : answer->market_series) {
            rows.push_back(market::summarise_series(row));
        }
        std::sort(rows.begin(), rows.end(),
            [](const market_series_summary& a, const market_series_summary& b) {
                return std::tie(a.asset_class, a.label, a.provider_code)
                    < std::tie(b.asset_class, b.label, b.provider_code);
            });

        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Read " << rows.size() << " market series on '"
            << options_.subject_prefix << "'";
        return rows;
    } catch (const std::exception& e) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The market series read failed: " << e.what();
        return transport_failure(e);
    } catch (...) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The market series read failed for an unnamed reason";
        return transport_failure();
    }
}

result<std::vector<hedgr::model::domain::market_provider_row>>
session_client::list_providers() const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a provider read needs an open connection to the instance");
    }
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a provider read needs a signed-in session");
    }

    using ores::marketdata::messaging::list_providers_request;
    using ores::marketdata::messaging::list_providers_response;

    try {
        std::vector<hedgr::model::domain::market_provider_row> rows;
        std::uint32_t offset = 0;
        for (;;) {
            list_providers_request request{};
            request.offset = offset;
            request.limit = 100;
            const auto answer =
                ores::nats::service::authenticated_request_and_decode<
                    list_providers_response>(client(),
                    list_providers_request::nats_subject, request,
                    options_.request_timeout);
            if (!answer) {
                return fail(session_errc::protocol_error,
                    "the provider answer could not be decoded");
            }
            if (answer->result.outcome != ores::utility::domain::outcome::ok) {
                return fail(session_errc::read_refused,
                    answer->result.message.empty()
                        ? std::string("the deployment refused the providers")
                        : answer->result.message);
            }
            for (const auto& provider : answer->providers) {
                rows.push_back(hedgr::model::domain::market_provider_row{
                    .code = provider.code,
                    .name = provider.name,
                    .kind = provider.kind});
            }
            offset += static_cast<std::uint32_t>(answer->providers.size());
            if (answer->providers.empty() || rows.size() >= answer->total)
                break;
        }
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Read " << rows.size() << " market providers on '"
            << options_.subject_prefix << "'";
        return rows;
    } catch (const std::exception& e) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The market provider read failed: " << e.what();
        return transport_failure(e);
    } catch (...) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The market provider read failed for an unnamed reason";
        return transport_failure();
    }
}

namespace {

/**
 * @brief The typed identity the service lists for one series.
 *
 * Asks for every series identity the session can see and picks the entry whose
 * key is @p series_id. A series the list does not carry cannot be read for
 * ticks by identity, and the failure says so, naming a truncated list as a
 * possible reason.
 */
[[nodiscard]] result<ores::marketdata::messaging::resolve_series_identity_request>
series_identity(ores::nats::service::nats_client& nats,
    std::string_view series_id, std::chrono::milliseconds timeout) {
    using ores::marketdata::messaging::list_series_identities_request;
    using ores::marketdata::messaging::list_series_identities_response;

    const auto reply = ores::nats::service::authenticated_request_and_decode<
        list_series_identities_response>(nats,
        list_series_identities_request::nats_subject,
        list_series_identities_request{}, timeout);
    if (!reply) {
        return fail(session_errc::protocol_error,
            "the series identities could not be decoded");
    }
    if (!reply->success) {
        return fail(session_errc::read_refused,
            reply->message.empty()
                ? std::string{"the series identities were refused"}
                : reply->message);
    }
    for (const auto& listed : reply->series) {
        if (listed.series_id == series_id)
            return listed.identity;
    }
    return fail(session_errc::read_refused,
        std::format("series '{}' is not among the {} identities the service "
                    "lists{}", series_id, reply->series.size(),
            reply->truncated ? " (the list was truncated)" : ""));
}

}

result<market_series_read>
session_client::market_series_ticks(std::string_view series_id,
    std::int64_t from_ns, std::int64_t to_ns,
    const hedgr::model::domain::market_series_identity& identity) const {
    if (!is_connected()) {
        return fail(session_errc::closed,
            "a tick read needs an open connection to the instance");
    }
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a tick read needs a signed-in session");
    }

    using ores::marketdata::messaging::get_series_ticks_request;
    using ores::marketdata::messaging::get_series_ticks_response;

    const auto instant = [](std::int64_t ns) {
        return std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::nanoseconds(ns))};
    };
    get_series_ticks_request request;
    request.from_instant = instant(from_ns);
    if (to_ns != 0)
        request.to_instant = instant(to_ns);

    try {
        /*
         * The call names a series by its typed identity, not by its key. The
         * catalogue parsed that identity from the series URI when it arrived,
         * so it is used directly. A series whose catalogue row predates that
         * parse falls back to the list the service publishes.
         */
        ores::marketdata::messaging::resolve_series_identity_request
            request_identity;
        if (!identity.empty()) {
            request_identity.asset = identity.asset;
            request_identity.scope = identity.scope;
            request_identity.instrument_type = identity.instrument_type;
            request_identity.quote_type = identity.quote_type;
            request_identity.party_id = identity.party_id;
            request_identity.provider_code = identity.provider_code;
            request_identity.fields.reserve(identity.fields.size());
            for (const auto& field : identity.fields) {
                request_identity.fields.push_back(
                    {.name = field.name, .text = field.text});
            }
        } else {
            auto listed = series_identity(client(), series_id,
                options_.request_timeout);
            if (!listed)
                return std::unexpected(std::move(listed.error()));
            request_identity = std::move(*listed);
        }
        request.identity = std::move(request_identity);
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Tick read of series '" << series_id << "' names identity "
            << "asset '" << request.identity.asset << "' scope '"
            << request.identity.scope << "' instrument '"
            << request.identity.instrument_type << "' quote '"
            << request.identity.quote_type << "' party '"
            << request.identity.party_id << "' with "
            << request.identity.fields.size() << " fields ("
            << (identity.empty() ? "listed" : "from the catalogue")
            << "; from_ns " << from_ns << ", to_ns " << to_ns << ")";

        const auto reply = ores::nats::service::authenticated_request_and_decode<
            get_series_ticks_response>(client(),
            get_series_ticks_request::nats_subject, request,
            options_.request_timeout);
        if (!reply) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The tick answer for series '" << series_id
                << "' could not be decoded (from_ns " << from_ns << ", to_ns "
                << to_ns << ")";
            return fail(session_errc::protocol_error,
                "the tick answer could not be decoded");
        }
        auto read = series_read_from_ticks(*reply);
        if (!read) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "The tick read of series '" << series_id << "' (from_ns "
                << from_ns << ", to_ns " << to_ns << ") failed: "
                << read.error().message;
            return read;
        }
        if (read->coordinates > 1) {
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Series '" << series_id << "' has " << read->coordinates
                << " coordinates; plotting the first, '" << read->coordinate
                << "'";
        }
        BOOST_LOG_SEV(lg(), ores::logging::debug)
            << "Read " << read->ticks << " ticks, " << read->points.size()
            << " of '" << read->coordinate << "', for series '" << series_id
            << "'";
        return read;
    } catch (const std::exception& e) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The tick read for series '" << series_id
            << "' failed: " << e.what();
        return transport_failure(e);
    } catch (...) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The tick read for series '" << series_id
            << "' failed for an unnamed reason";
        return transport_failure();
    }
}

namespace {

[[nodiscard]] std::int64_t clock_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}

result<market_series_read>
session_client::market_series_points(std::string_view series_id,
    const hedgr::model::domain::market_series_identity& identity) const {
    const tick_fetch fetch
        = [this, series_id, &identity](const tick_span& span) {
              return market_series_ticks(series_id, span.from_ns, span.to_ns,
                  identity);
          };
    return read_first_load(fetch, clock_now_ns());
}

result<market_series_read>
session_client::market_series_tail(std::string_view series_id,
    std::size_t count) const {
    const tick_fetch fetch = [this, series_id](const tick_span& span) {
        return market_series_ticks(series_id, span.from_ns, span.to_ns);
    };
    return read_newest_ticks(fetch, clock_now_ns(),
        hedgr::model::domain::market_tail_spans, count);
}

result<market_series_read>
session_client::market_series_history(std::string_view series_id,
    std::int64_t from_ns, std::int64_t to_ns,
    const hedgr::model::domain::market_series_identity& identity) const {
    const tick_fetch fetch
        = [this, series_id, &identity](const tick_span& span) {
              return market_series_ticks(series_id, span.from_ns, span.to_ns,
                  identity);
          };
    return read_tick_span(fetch, tick_span{from_ns, to_ns}, to_ns, 1);
}

std::string session_client::subject(std::string_view suffix) const {
    return make_subject(options_, suffix);
}

const session_account& session_client::account() const noexcept {
    return account_;
}

const session_options& session_client::options() const noexcept {
    return options_;
}

}
