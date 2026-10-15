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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_CLIENT_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_CLIENT_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_account.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <string>
#include <string_view>
#include "ores.nats/service/nats_client.hpp"

namespace hedgr::session::transport {

/**
 * @brief What a deployment says about its own setup.
 *
 * The answer to the one question that can be asked before anybody signs in, so
 * it is also the question a probe asks: a deployment that still needs its first
 * administrator is not ready, and one that has been set up is.
 */
struct bootstrap_status final {
    /**
     * @brief Whether the deployment still needs its first administrator.
     *
     * True means setup is incomplete. It is the deployment's own words, read
     * from the setting that governs whether the initial-admin screen is the
     * one a person belongs on.
     */
    bool is_in_bootstrap_mode = false;

    /** @brief Whether the deployment has a tenant that somebody set up. */
    bool has_tenant = false;

    /**
     * @brief What IAM said about its own state.
     *
     * Empty on the success path, because IAM only fills this in to explain a
     * failure.
     */
    std::string message;

    /** @brief The build the answering service runs, in full. */
    std::string version;
};

/**
 * @brief One blocking connection from one account to one instance.
 *
 * Every call here blocks until the instance answers or the request times
 * out, because the transport underneath is blocking and pretending otherwise
 * would be a lie. Concurrency is the caller's: the runtime runs each session
 * on a thread-pool thread, so a blocked call costs one pool thread rather than
 * the frame loop. Nothing in this class spawns a thread, a coroutine or an
 * executor, and nothing in it is safe to call from two threads at once.
 *
 * No exception escapes a public method: every failure, including the ones the
 * wrapped client throws, arrives as a session_error a window can render.
 */
class HEDGR_SESSION_EXPORT session_client final {
public:
    /**
     * @brief Binds a session to one instance's options.
     *
     * Nothing is opened: this only records where to connect and how.
     */
    explicit session_client(session_options options);

    /**
     * @brief Disconnects if the session is still open.
     *
     * A session owns its transport, so letting it go must not leave a socket
     * behind for the caller to remember to close.
     */
    ~session_client();

    session_client(const session_client&) = delete;
    session_client& operator=(const session_client&) = delete;

    /**
     * @brief Opens the connection to the instance.
     *
     * Idempotent: connecting a session that is already open succeeds and does
     * nothing, so a retry after a partial failure converges on one connection
     * rather than a second one.
     *
     * @return nothing on success, or why the connection could not be opened.
     */
    [[nodiscard]] result<void> connect();

    /**
     * @brief Drops the credential, then closes the connection.
     *
     * Safe to call twice, and safe on a session that was never connected, so a
     * teardown path need not ask whether there is anything to tear down.
     */
    void disconnect();

    /** @brief Whether a connection to the instance is open. */
    [[nodiscard]] bool is_connected() const noexcept;

    /** @brief Whether this session holds a credential the instance issued. */
    [[nodiscard]] bool is_authenticated() const noexcept;

    /**
     * @brief Signs in, and holds the token the instance issues.
     *
     * The token is a secret kept for the session's lifetime and never
     * persisted. A refusal from the instance is reported as
     * session_errc::authentication_rejected carrying the instance's own
     * message, because that message names the reason a person must act on.
     *
     * An account with more than one party signs in with a single-use token
     * that only select_party may present; the caller follows this call with
     * select_party() in that case.
     *
     * @param principal the username presented to the instance.
     * @param password the secret presented to the instance.
     * @return who the instance says this session is, or why it refused.
     */
    [[nodiscard]] result<session_account>
    authenticate(std::string_view principal, std::string_view password);

    /**
     * @brief Scopes an already signed-in session to one party.
     *
     * Exchanges the current token for one carrying the chosen party, which is
     * also what a multi-party sign-in must do before anything else.
     *
     * @pre the session is authenticated.
     * @param party_id the party to scope the session to.
     * @return nothing on success, or why the instance refused.
     */
    [[nodiscard]] result<void> select_party(std::string_view party_id);

    /**
     * @brief Signs out, and gives up the token whatever the instance says.
     *
     * A sign-out is a state rather than an event: the point of it is that this
     * process no longer holds a credential, so the token is dropped even when
     * the request cannot be delivered, and signing out of a session that holds
     * no credential succeeds because that state already holds. The result
     * reports whether the instance also recorded the sign-out; a caller
     * that is signing out because the session is already broken may ignore it.
     *
     * The connection is left open, so a caller may sign in again over it.
     *
     * @return nothing when the instance recorded the sign-out.
     */
    [[nodiscard]] result<void> logout();

    /**
     * @brief Sends a request that needs no credential.
     *
     * For the calls that come before a sign-in, the sign-in among them.
     *
     * @pre the session is connected.
     * @param relative_subject the subject as the deployment names it, such as
     * "iam.v1.ops.login"; the instance's prefix is applied by the transport,
     * so pass subject()'s answer only when you want the full wire name.
     * @param json the request body, already encoded in the session's format.
     * @return the reply body.
     * @note this path has no timeout knob in the wrapped client, so it waits
     * on the transport's own default rather than on request_timeout.
     */
    [[nodiscard]] result<std::string>
    request(std::string_view relative_subject, std::string_view json) const;

    /**
     * @brief Sends a request carrying this session's bearer token.
     *
     * @pre the session is authenticated; without a credential this fails with
     * session_errc::not_authenticated rather than sending an anonymous call
     * that the instance would reject anyway.
     * @param relative_subject the subject as the deployment names it.
     * @param json the request body, already encoded in the session's format.
     * @return the reply body.
     */
    [[nodiscard]] result<std::string>
    authenticated_request(std::string_view relative_subject,
        std::string_view json) const;

    /**
     * @brief Asks the deployment whether it has been bootstrapped.
     *
     * The one call a probe makes, and the one call that needs no credential:
     * the deployment cannot ask who is calling before it has anywhere to look
     * the caller up, so this goes through the unauthenticated request path and
     * nowhere else. There is no second transport here and no second subject.
     *
     * @pre the session is connected.
     * @return what the deployment said about itself, or why it did not say.
     * A broker with no IAM behind it answers nothing at all, which arrives as
     * session_errc::services_unavailable: the broker is up, the services are
     * not, and the caller is owed that distinction.
     */
    [[nodiscard]] result<bootstrap_status> bootstrap_status() const;

    /**
     * @brief The full wire name of one of this instance's subjects.
     *
     * The prefix joined to the suffix, for a log line, a tooltip or a
     * subscription that must name the instance's namespace. Requests take the
     * suffix instead, since the transport applies the prefix itself.
     */
    [[nodiscard]] std::string subject(std::string_view suffix) const;

    /** @brief Who this session is, empty until the instance says. */
    [[nodiscard]] const session_account& account() const noexcept;

    /** @brief Where this session connects and how. */
    [[nodiscard]] const session_options& options() const noexcept;

private:
    /**
     * @brief The wrapped client, for the two request methods.
     *
     * The wrapped client qualifies its request methods non-const although a
     * request changes nothing a caller here can observe, which is what lets
     * the two request methods above be const and reachable through a const
     * session.
     */
    [[nodiscard]] ores::nats::service::nats_client& client() const;

    ores::nats::service::nats_client nats_;
    session_options options_;
    session_account account_;
    bool connected_ = false;
};

}

#endif
