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
    case command_kind::save_folder:
        return "save_folder";
    case command_kind::remove_folder:
        return "remove_folder";
    case command_kind::save_label:
        return "save_label";
    case command_kind::remove_label:
        return "remove_label";
    case command_kind::attach_label:
        return "attach_label";
    case command_kind::detach_label:
        return "detach_label";
    case command_kind::probe_instance:
        return "probe_instance";
    case command_kind::probe_all:
        return "probe_all";
    case command_kind::set_master_password:
        return "set_master_password";
    case command_kind::unlock_store:
        return "unlock_store";
    case command_kind::lock_store:
        return "lock_store";
    case command_kind::save_shortcut:
        return "save_shortcut";
    case command_kind::remove_shortcut:
        return "remove_shortcut";
    case command_kind::store_credential:
        return "store_credential";
    case command_kind::load_image:
        return "load_image";
    case command_kind::load_term_structure:
        return "load_term_structure";
    case command_kind::configure_crm:
        return "configure_crm";
    case command_kind::load_crm_names:
        return "load_crm_names";
    case command_kind::load_history:
        return "load_history";
    }
    return "unknown";
}

}
