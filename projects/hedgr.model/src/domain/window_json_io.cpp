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
#include "hedgr.model/domain/window_json_io.hpp"
#include <format>
#include <rfl/json.hpp>
#include <stdexcept>
#include <utility>

namespace hedgr::model::domain {

namespace {

template <typename Shape>
std::expected<std::string, std::string> write_shape(const window_state& state) {
    const auto* shape = std::get_if<Shape>(&state);
    if (shape == nullptr)
        return std::unexpected("window state does not match its kind");
    try {
        return rfl::json::write(*shape);
    } catch (const std::runtime_error& error) {
        return std::unexpected(
            std::format("could not write window state: {}", error.what()));
    }
}

template <typename Shape>
std::expected<window_state, std::string> read_shape(std::string_view json) {
    auto parsed = rfl::json::read<Shape, rfl::DefaultIfMissing>(json);
    if (!parsed)
        return std::unexpected(
            std::format("could not read window state: {}", parsed.error().what()));
    return window_state{std::move(parsed.value())};
}

}

std::expected<std::string, std::string>
window_state_to_json(window_kind kind, const window_state& state) {
    switch (kind) {
    case window_kind::chart:
        return write_shape<chart_state>(state);
    case window_kind::grid:
        return write_shape<grid_state>(state);
    case window_kind::notice:
    case window_kind::instance_manager:
        return write_shape<notice_state>(state);
    case window_kind::accounts:
        return write_shape<accounts_state>(state);
    case window_kind::workspace:
        return write_shape<workspace_state>(state);
    case window_kind::command_centre:
        return write_shape<command_centre_state>(state);
    case window_kind::series_browser:
        return write_shape<series_browser_state>(state);
    case window_kind::term_structure:
        return write_shape<term_structure_state>(state);
    case window_kind::crm_matrix:
        return write_shape<crm_matrix_state>(state);
    case window_kind::mini_plot:
        return write_shape<mini_plot_state>(state);
    case window_kind::vol_surface:
        return write_shape<vol_surface_state>(state);
    }
    return std::unexpected("unknown window kind");
}

std::expected<window_state, std::string>
window_state_from_json(window_kind kind, std::string_view json) {
    switch (kind) {
    case window_kind::chart:
        return read_shape<chart_state>(json);
    case window_kind::grid:
        return read_shape<grid_state>(json);
    case window_kind::notice:
    case window_kind::instance_manager:
        return read_shape<notice_state>(json);
    case window_kind::accounts:
        return read_shape<accounts_state>(json);
    case window_kind::workspace:
        return read_shape<workspace_state>(json);
    case window_kind::command_centre:
        return read_shape<command_centre_state>(json);
    case window_kind::series_browser:
        return read_shape<series_browser_state>(json);
    case window_kind::term_structure:
        return read_shape<term_structure_state>(json);
    case window_kind::crm_matrix:
        return read_shape<crm_matrix_state>(json);
    case window_kind::mini_plot:
        return read_shape<mini_plot_state>(json);
    case window_kind::vol_surface:
        return read_shape<vol_surface_state>(json);
    }
    return std::unexpected("unknown window kind");
}

}
