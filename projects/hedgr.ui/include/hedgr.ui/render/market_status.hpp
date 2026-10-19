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
#ifndef HEDGR_UI_RENDER_MARKET_STATUS_HPP
#define HEDGR_UI_RENDER_MARKET_STATUS_HPP

#include "hedgr.ui/render/data_health.hpp"
#include "hedgr.ui/theme/theme.hpp"

namespace hedgr::ui::render {

/**
 * @brief The colour every market-data screen uses for a freshness state.
 *
 * One mapping rather than one per window. A stale FX chart and a stale IR curve
 * are the same statement, and two copies of the colour choice would let them
 * drift into two different reds.
 */
[[nodiscard]] inline ImU32 health_colour(data_health state) {
    switch (state) {
    case data_health::live:
        return hedgr::ui::theme::health_live_colour;
    case data_health::delayed:
        return hedgr::ui::theme::readiness_colour(
            hedgr::model::domain::readiness::bootstrapping);
    case data_health::stale:
        return hedgr::ui::theme::failure_colour;
    case data_health::unavailable:
        return ImGui::GetColorU32(ImGuiCol_TextDisabled);
    case data_health::historical:
        return ImGui::GetColorU32(ImGuiCol_Text);
    }
    return ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

}

#endif
