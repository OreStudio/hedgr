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
#include "hedgr.model/domain/session.hpp"

namespace hedgr::model::domain {

std::string_view to_string(session_status value) noexcept {
    switch (value) {
    case session_status::disconnected:
        return "disconnected";
    case session_status::connecting:
        return "connecting";
    case session_status::authenticated:
        return "authenticated";
    case session_status::failed:
        return "failed";
    }
    return "unknown";
}

std::string_view to_string(session_mode value) noexcept {
    switch (value) {
    case session_mode::application:
        return "application";
    case session_mode::system_administration:
        return "system_administration";
    }
    return "unknown";
}

session_mode session_mode_for(std::string_view tenant_id) noexcept {
    return tenant_id == system_tenant_id ? session_mode::system_administration
                                         : session_mode::application;
}

}
