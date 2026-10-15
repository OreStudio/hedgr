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
#ifndef HEDGR_MODEL_DOMAIN_WINDOW_JSON_IO_HPP
#define HEDGR_MODEL_DOMAIN_WINDOW_JSON_IO_HPP

#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/export.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::model::domain {

/**
 * @brief Serialises a window's state, given the kind it belongs to.
 *
 * The kind travels beside the payload rather than inside it, so no variant is
 * ever written to storage. A payload whose alternative does not match the kind
 * is a programming error and is reported rather than silently coerced.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<std::string, std::string>
window_state_to_json(window_kind kind, const window_state& state);

/**
 * @brief Parses a window's state for a known kind.
 *
 * This is the boundary where stored text becomes a typed state again, so a
 * malformed payload is reported with its parse failure rather than raising.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<window_state, std::string>
window_state_from_json(window_kind kind, std::string_view json);

}

#endif
