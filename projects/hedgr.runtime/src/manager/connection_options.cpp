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
#include "hedgr.runtime/manager/connection_options.hpp"

namespace hedgr::runtime::manager {

namespace transport = hedgr::session::transport;

std::string resolve_tls_path(std::string_view path,
    const std::filesystem::path& database_path) {
    /*
     * Empty first, because an empty path is a value with a meaning -- "fall
     * back to the environment" -- and joining it onto the store's folder would
     * turn that meaning into the store's own folder, which is a file nobody
     * named.
     */
    if (path.empty())
        return {};

    const std::filesystem::path given{path};
    if (given.is_absolute())
        return given.string();

    const auto directory = database_path.parent_path();
    if (directory.empty())
        return given.string();

    /*
     * Normalised, so "keys/../keys/ca.crt" and "./ca.crt" read back as the file
     * they name. A path is shown to the operator when it cannot be opened, and
     * a sentence about a file that is spelled differently from the one on disk
     * is a sentence that sends them looking in the wrong place.
     */
    return (directory / given).lexically_normal().string();
}

transport::session_options connection_options_for(const model::domain::instance& row,
    const transport::session_options& seed,
    const std::filesystem::path& database_path) {
    transport::session_options options;
    options.nats_url = row.nats_url;
    options.subject_prefix = row.subject_prefix;
    options.wire_format = row.wire_format;

    const auto resolved = [&database_path](std::string_view stored,
                                            std::string_view fallback) {
        return stored.empty() ? std::string{fallback}
                              : resolve_tls_path(stored, database_path);
    };

    /*
     * A path only the environment knows is already absolute or already
     * meaningful where the process runs, so it is copied rather than resolved:
     * joining it onto the store's folder would invent a location for a file the
     * operator named outright.
     */
    options.tls_ca = resolved(row.tls_ca, seed.tls_ca);
    options.tls_cert = resolved(row.tls_cert, seed.tls_cert);
    options.tls_key = resolved(row.tls_key, seed.tls_key);

    options.request_timeout = seed.request_timeout;
    options.connect_timeout = seed.connect_timeout;
    return options;
}

}
