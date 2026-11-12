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
#include "hedgr.model/event/event.hpp"

namespace hedgr::model::event {

std::string_view to_string(event_kind value) noexcept {
    switch (value) {
    case event_kind::session_status_changed:
        return "session_status_changed";
    case event_kind::session_authenticated:
        return "session_authenticated";
    case event_kind::session_failed:
        return "session_failed";
    case event_kind::probe_result:
        return "probe_result";
    case event_kind::store_unlocked:
        return "store_unlocked";
    case event_kind::store_lock_failed:
        return "store_lock_failed";
    case event_kind::password_policy_fetched:
        return "password_policy_fetched";
    case event_kind::deployment_snapshot:
        return "deployment_snapshot";
    case event_kind::series_catalogue:
        return "series_catalogue";
    case event_kind::series_points:
        return "series_points";
    case event_kind::series_history:
        return "series_history";
    case event_kind::currency_image_map:
        return "currency_image_map";
    case event_kind::curve_sets:
        return "curve_sets";
    case event_kind::image_loaded:
        return "image_loaded";
    case event_kind::term_structure:
        return "term_structure";
    case event_kind::crm_rates:
        return "crm_rates";
    case event_kind::crm_names:
        return "crm_names";
    case event_kind::session_token_refreshed:
        return "session_token_refreshed";
    case event_kind::session_party_changed:
        return "session_party_changed";
    case event_kind::session_party_refused:
        return "session_party_refused";
    case event_kind::master_sealed:
        return "master_sealed";
    case event_kind::link_lost:
        return "link_lost";
    case event_kind::link_restored:
        return "link_restored";
    }
    return "unknown";
}

}
