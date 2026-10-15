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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_OPTIONS_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_OPTIONS_HPP

#include <chrono>
#include <string>
#include <string_view>

namespace hedgr::session::transport {

/**
 * @brief Everything needed to open one connection to one instance.
 *
 * A plain value: it is what an instance and an identity resolve to before any
 * connection exists, so it can be copied, stored and compared without holding
 * a socket. The caller builds it once per session; the transport reads it and
 * never writes it back.
 */
struct session_options final {
    /** @brief The broker address, e.g. "nats://localhost:20405". */
    std::string nats_url;

    /** @brief The subject prefix the instance's services listen on. */
    std::string subject_prefix;

    /** @brief The body encoding, "msgpack" or "json". */
    std::string wire_format = "msgpack";

    /** @brief Path to the CA certificate; empty means no TLS. */
    std::string tls_ca;

    /** @brief Path to this client's certificate. */
    std::string tls_cert;

    /** @brief Path to this client's private key. */
    std::string tls_key;

    /**
     * @brief How long a request waits for its reply.
     *
     * Honoured by the signed-in paths. The wrapped client gives the plain
     * request path no timeout knob, so a signed-out request waits on the
     * transport's own default instead.
     */
    std::chrono::milliseconds request_timeout{30000};

    /**
     * @brief How long a connection attempt may take.
     *
     * Recorded but not yet honoured: the wrapped client exposes no connect
     * timeout, so a connection attempt takes as long as it takes.
     */
    std::chrono::milliseconds connect_timeout{10000};

    /**
     * @brief Whether these options ask for a secure connection.
     *
     * The CA is the switch rather than the whole triple: the transport turns
     * TLS on exactly when it is given a CA to trust, so cert and key without a
     * CA would silently connect in the clear.
     */
    [[nodiscard]] bool uses_tls() const noexcept {
        return !tls_ca.empty();
    }
};

/**
 * @brief The subject an instance's suffix is named by on the wire.
 *
 * A suffix is what the deployment's own protocols call a subject, such as
 * "iam.v1.ops.login"; the instance's prefix is added here and nowhere else, so
 * a prefix that already ends in a dot does not produce a doubled one.
 */
[[nodiscard]] inline std::string make_subject(const session_options& options,
    std::string_view suffix) {
    std::string r(options.subject_prefix);
    if (r.empty())
        return std::string(suffix);
    if (r.back() != '.')
        r.push_back('.');
    r.append(suffix);
    return r;
}

}

#endif
