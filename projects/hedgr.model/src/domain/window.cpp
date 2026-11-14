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
#include "hedgr.model/domain/window.hpp"
#include <format>

namespace hedgr::model::domain {

std::string_view to_string(window_kind value) noexcept {
    switch (value) {
    case window_kind::chart:
        return "chart";
    case window_kind::grid:
        return "grid";
    case window_kind::notice:
        return "notice";
    case window_kind::instance_manager:
        return "instance_manager";
    case window_kind::accounts:
        return "accounts";
    case window_kind::workspace:
        return "workspace";
    case window_kind::command_centre:
        return "command_centre";
    case window_kind::series_browser:
        return "series_browser";
    case window_kind::term_structure:
        return "term_structure";
    case window_kind::crm_matrix:
        return "crm_matrix";
    case window_kind::mini_plot:
        return "mini_plot";
    case window_kind::vol_surface:
        return "vol_surface";
    case window_kind::rate_ladder:
        return "rate_ladder";
    }
    return "unknown";
}

std::expected<window_kind, std::string>
window_kind_from_string(std::string_view text) {
    if (text == "chart")
        return window_kind::chart;
    if (text == "grid")
        return window_kind::grid;
    if (text == "notice")
        return window_kind::notice;
    if (text == "instance_manager")
        return window_kind::instance_manager;
    if (text == "accounts")
        return window_kind::accounts;
    if (text == "workspace")
        return window_kind::workspace;
    if (text == "command_centre")
        return window_kind::command_centre;
    if (text == "series_browser")
        return window_kind::series_browser;
    if (text == "term_structure")
        return window_kind::term_structure;
    if (text == "crm_matrix")
        return window_kind::crm_matrix;
    if (text == "mini_plot")
        return window_kind::mini_plot;
    if (text == "vol_surface")
        return window_kind::vol_surface;
    if (text == "rate_ladder")
        return window_kind::rate_ladder;
    return std::unexpected(std::format("unknown window kind '{}'", text));
}

}
