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
#include "hedgr.session/transport/session_error.hpp"

#include <string>
#include <utility>
#include "ores.nats/service/nats_connect_error.hpp"

namespace {

using hedgr::session::transport::session_errc;

/**
 * @brief The sentence a person reads for each code.
 *
 * Kept in one place so a failure means the same thing to a reader whichever
 * call produced it, and so rewording a message is one edit rather than a hunt.
 */
[[nodiscard]] std::string_view message_for(session_errc code) noexcept {
    using enum session_errc;

    switch (code) {
        case unreachable:
            return "the instance is not reachable";
        case tls_failed:
            return "the secure connection to the instance could not be established";
        case timeout:
            return "the instance did not answer in time";
        case services_unavailable:
            /*
             * The commonest failure in practice: the broker is up but the
             * services behind it are not, so nothing answers. A reader cannot
             * act on "no responders" -- only an administrator can.
             */
            return "the instance is not responding; contact your system administrator";
        case authentication_rejected:
            return "the instance rejected the credentials";
        case not_authenticated:
            return "this session is not signed in";
        case protocol_error:
            return "the instance answered something this client could not understand";
        case closed:
            return "the session is not connected";
    }
    return "the session could not be used";
}

/**
 * @brief The session code a NATS connection failure maps onto.
 */
[[nodiscard]] session_errc
to_session_errc(ores::nats::service::nats_error_kind kind) noexcept {
    using ores::nats::service::nats_error_kind;
    using enum hedgr::session::transport::session_errc;

    switch (kind) {
        case nats_error_kind::server_unreachable:
            return unreachable;
        case nats_error_kind::tls_error:
            return tls_failed;
        case nats_error_kind::connection_timeout:
            return timeout;
        case nats_error_kind::services_unavailable:
            return services_unavailable;
        case nats_error_kind::other:
            return protocol_error;
    }
    return protocol_error;
}

}

namespace hedgr::session::transport {

std::string_view to_string(session_errc value) noexcept {
    using enum session_errc;

    switch (value) {
        case unreachable:
            return "unreachable";
        case tls_failed:
            return "tls_failed";
        case timeout:
            return "timeout";
        case services_unavailable:
            return "services_unavailable";
        case authentication_rejected:
            return "authentication_rejected";
        case not_authenticated:
            return "not_authenticated";
        case protocol_error:
            return "protocol_error";
        case closed:
            return "closed";
    }
    return "closed";
}

session_error make_error(session_errc code, std::string detail) {
    return session_error{
        .code = code,
        .message = std::string(message_for(code)),
        .detail = std::move(detail)};
}

session_error translate_connect_error(const std::exception& error) {
    const auto* connect =
        dynamic_cast<const ores::nats::service::nats_connect_error*>(&error);

    /*
     * A failure that is not a connection failure is treated as a protocol
     * error: every call into the transport happens on a connection that was
     * established, so anything else is the deployment answering, or the
     * connection dropping, in a way this client has no better name for.
     */
    const auto code = connect == nullptr
        ? session_errc::protocol_error
        : to_session_errc(connect->kind());

    return make_error(code, error.what());
}

}
