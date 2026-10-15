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

#include <format>
#include <string>
#include <utility>
#include "ores.iam.api/messaging/account_operations_protocol.hpp"
#include "ores.iam.api/messaging/login_protocol.hpp"
#include "ores.nats/domain/wire_format.hpp"
#include "ores.nats/service/nats_connect_error.hpp"
#include "ores.nats/service/request_helpers.hpp"

namespace {

using hedgr::session::transport::fail;
using hedgr::session::transport::session_errc;
using hedgr::session::transport::session_error;
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
        return fail(session_errc::protocol_error,
            std::format("the wire format '{}' is not one this client understands",
                options_.wire_format));
    }

    ores::nats::config::nats_options opts;
    opts.url = options_.nats_url;
    opts.subject_prefix = options_.subject_prefix;
    opts.format = *format;
    if (options_.uses_tls()) {
        opts.tls_ca_cert = options_.tls_ca;
        opts.tls_client_cert = options_.tls_cert;
        opts.tls_client_key = options_.tls_key;
    }

    /*
     * connect_timeout has no counterpart in the wrapped client's options, so
     * it is recorded for the caller and not yet honoured on the wire.
     */
    try {
        nats_.connect(std::move(opts));
    } catch (const std::exception& e) {
        connected_ = false;
        return connect_failure(e);
    } catch (...) {
        connected_ = false;
        return connect_failure();
    }

    connected_ = true;
    return {};
}

void session_client::disconnect() {
    /*
     * The credential goes first: a connection dropped while a token is still
     * held is a session that could be resumed by accident.
     */
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

        account_ = session_account{
            .token = answer.token,
            .tenant_id = answer.tenant_id,
            .tenant_name = answer.tenant_name,
            .party_name = std::string(),
            .principal = answer.username.empty()
                ? std::string(principal) : answer.username};

        return account_;
    } catch (const std::exception& e) {
        return transport_failure(e);
    } catch (...) {
        return transport_failure();
    }
}

result<void> session_client::select_party(std::string_view party_id) {
    if (!is_authenticated()) {
        return fail(session_errc::not_authenticated,
            "a party can only be chosen by a signed-in session");
    }

    using ores::iam::messaging::select_party_request;
    using ores::iam::messaging::select_party_response;

    try {
        const select_party_request request{.party_id = std::string(party_id)};

        const auto reply =
            ores::nats::service::authenticated_request_and_decode<select_party_response>(
                nats_, select_party_request::nats_subject, request,
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
                    select_party_request::nats_subject)});
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
        account_.party_name = answer.party_name;
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
