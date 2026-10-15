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
#ifndef HEDGR_RUNTIME_MANAGER_CONNECTION_OPTIONS_HPP
#define HEDGR_RUNTIME_MANAGER_CONNECTION_OPTIONS_HPP

#include "hedgr.model/domain/instance.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace hedgr::runtime::manager {

/**
 * @brief A path from an instance row, made absolute against the store's folder.
 *
 * An instance's TLS material is stored as the operator typed it, and a relative
 * path there is relative to the store rather than to whatever directory the
 * client happens to have been started in. That is the whole point of storing it
 * relative: a store copied to another host -- with its key material beside it,
 * which is the only thing the copy is for -- resolves to the same files, and
 * the operator does not have to know where the process was launched from.
 *
 * @param path the stored path; empty is returned empty rather than resolved, so
 * "no path" stays distinguishable from "this directory".
 * @param database_path the store's own file, as @c sqlite::database::path
 * reports it. A store with no file -- an in-memory one -- has no folder to
 * resolve against, so a relative path is returned as written.
 *
 * @return The absolute path, or @p path unchanged when it is already absolute.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT std::string
resolve_tls_path(std::string_view path, const std::filesystem::path& database_path);

/**
 * @brief The options one instance's connection is built from.
 *
 * An instance names its own TLS material when its form was filled in, and falls
 * back to what the process environment supplied for each path it leaves empty.
 * The fallback is per path rather than all-or-nothing, because the three
 * settings are not one thing: a CA is the trust anchor for the deployment and a
 * certificate is who this client says it is, so an operator may sensibly trust
 * a different authority while still presenting the identity the machine
 * already has.
 *
 * The timeouts are never per instance: how long to wait is a property of the
 * client and the network, and a stored row has no opinion about it.
 *
 * @param row the instance to connect to.
 * @param seed what the process was started with, used for every path the row
 * leaves empty.
 * @param database_path the store's own file, which is the directory a relative
 * path resolves against.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT session::transport::session_options
connection_options_for(const model::domain::instance& row,
    const session::transport::session_options& seed,
    const std::filesystem::path& database_path);

}

#endif
