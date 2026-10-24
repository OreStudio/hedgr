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

#include "hedgr.model/domain/health.hpp"
#include "hedgr.ui/render/time_text.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @brief The colour every market-data screen uses for a freshness state.
 *
 * One mapping rather than one per window. A stale FX chart and a stale IR curve
 * are the same statement, and two copies of the colour choice would let them
 * drift into two different reds.
 */
[[nodiscard]] inline ImU32 health_colour(
    hedgr::model::domain::data_health state) {
    using hedgr::model::domain::data_health;
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
    }
    return ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

/**
 * @brief Puts one health verdict on a market window's foot.
 *
 * Every market window states its health through this, so the word, the colour
 * and the as-of instant cannot drift apart from one window to the next. The
 * age against @p wall_now_ns rides with them and is drawn in the state's
 * colour, so a late feed and a dead one are told apart without opening a log.
 */
inline void set_health_foot(hedgr::ui::widget::foot_state& foot,
    hedgr::model::domain::data_health state, std::int64_t as_of_ns,
    std::int64_t wall_now_ns, bool reading) {
    foot.word = std::string{hedgr::model::domain::data_health_word(state)};
    foot.colour = health_colour(state);
    foot.as_of = as_of_text(as_of_ns);
    foot.age = as_of_ns > 0 && wall_now_ns > 0
        ? tick_age_text(wall_now_ns - as_of_ns) : std::string{};
    foot.updating = reading;
}

/**
 * @brief Says in the foot that the series' history could not be read.
 *
 * A read that failed leaves the chart showing only what arrived live, which the
 * foot's health word would otherwise report as a healthy feed. The foot carries
 * the short note, and the reason is in a tooltip over it.
 */
inline void set_history_unavailable_foot(
    hedgr::ui::widget::foot_state& foot, std::string_view failure) {
    if (failure.empty())
        return;
    foot.word = "history unavailable";
    foot.colour = theme::failure_colour;
    foot.detail = std::string{failure};
}

}

#endif
