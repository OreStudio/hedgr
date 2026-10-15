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
#include "hedgr.model/command/command.hpp"

namespace hedgr::model::command {

std::string_view to_string(command_kind value) noexcept {
    switch (value) {
    case command_kind::save_instance:
        return "save_instance";
    case command_kind::remove_instance:
        return "remove_instance";
    case command_kind::save_account:
        return "save_account";
    case command_kind::remove_account:
        return "remove_account";
    case command_kind::connect_session:
        return "connect_session";
    case command_kind::disconnect_session:
        return "disconnect_session";
    case command_kind::open_window:
        return "open_window";
    case command_kind::close_window:
        return "close_window";
    case command_kind::refresh_window:
        return "refresh_window";
    case command_kind::shutdown:
        return "shutdown";
    }
    return "unknown";
}

}
