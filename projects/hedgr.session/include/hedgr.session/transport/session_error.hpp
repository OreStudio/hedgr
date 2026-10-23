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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_ERROR_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_ERROR_HPP

#include "hedgr.session/export.hpp"
#include <cstdint>
#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::session::transport {

/**
 * @brief Why a session operation failed, in the terms a window renders.
 *
 * One code per thing a person can be told to do about it: wait for the
 * instance, ask an administrator, fix the certificate, sign in again. The
 * transport's own vocabulary -- NATS statuses, TLS handshake reasons, "no
 * responders" -- is deliberately absent, because none of it tells a reader
 * what to do next.
 */
enum class session_errc : std::uint8_t {
    /** @brief The broker could not be reached at all. */
    unreachable = 0,
    /** @brief The secure connection could not be established. */
    tls_failed = 1,
    /** @brief The instance did not answer in time. */
    timeout = 2,
    /** @brief The broker is up but the services behind it are not. */
    services_unavailable = 3,
    /** @brief The instance refused the credentials it was presented. */
    authentication_rejected = 4,
    /** @brief The operation needs a signed-in session and there is none. */
    not_authenticated = 5,
    /** @brief The answer arrived but could not be understood. */
    protocol_error = 6,
    /** @brief There is no open connection to send on. */
    closed = 7,
    /** @brief The instance ended the login session, so the token is dead. */
    session_expired = 8,
    /** @brief The tick span holds more ticks than one reply carries. */
    range_too_large = 9,
    /** @brief The session has no party, so it can read no ticks. */
    party_required = 10,
    /** @brief The tick span ends before it starts. */
    range_inverted = 11,
    /** @brief The tick read names more coordinates than the service allows. */
    filter_too_large = 12,
    /** @brief The deployment refused the tick read for another reason. */
    read_refused = 13,
};

/**
 * @brief A failure, split into what to say and what to log.
 *
 * @c message is the sentence a person reads; @c detail is the low-level cause,
 * which the interface shows dimmed beside it and a log keeps in full. Two
 * fields rather than one, because the person who cannot sign in needs to know
 * who to ask, while the administrator needs the subject that had no responder.
 */
struct session_error final {
    /** @brief Which failure this is. */
    session_errc code = session_errc::closed;

    /** @brief The user-facing sentence. */
    std::string message;

    /** @brief The low-level cause, shown dimmed. */
    std::string detail;
};

/** @brief A session operation that either produces a value or fails. */
template <typename T>
using result = std::expected<T, session_error>;

/**
 * @brief The stable spelling of a code, for logs and tooltips.
 *
 * Distinct from session_error::message, which is prose for a person and may be
 * reworded; this is what a log line and a test compare against.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string_view
to_string(session_errc value) noexcept;

/**
 * @brief Builds an error with the canonical sentence for its code.
 *
 * Every failure the transport raises itself goes through here, so one code has
 * one sentence no matter which call produced it.
 *
 * @param code which failure this is.
 * @param detail the low-level cause, kept verbatim for the log and the dimmed
 * line under the message.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT session_error
make_error(session_errc code, std::string detail);

/**
 * @brief Builds a failure that is ready to be returned from a result.
 *
 * std::expected takes its error through std::unexpected, which is not a
 * conversion a plain return can make, so without this every return site that
 * raises a failure of its own would spell the wrapper around the error it just
 * built.
 */
[[nodiscard]] inline std::unexpected<session_error> fail(session_errc code,
    std::string detail) {
    return std::unexpected<session_error>(make_error(code, std::move(detail)));
}

/**
 * @brief Translates a failure thrown by the wrapped NATS client.
 *
 * The ORE transport reports a connection failure as a nats_connect_error
 * carrying a nats_error_kind, which is close enough to the vocabulary above to
 * map directly; anything else it throws becomes a protocol error, since the
 * only way to reach the transport is through a connection that should have
 * worked. The thrown text is preserved in session_error::detail.
 *
 * @param error the exception the transport threw.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT session_error
translate_connect_error(const std::exception& error);

}

#endif
