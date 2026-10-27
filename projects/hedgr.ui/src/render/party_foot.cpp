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
#include "hedgr.ui/render/party_foot.hpp"

#include "hedgr.ui/render/series_browser_view.hpp"
#include "hedgr.ui/widget/status_foot.hpp"

namespace hedgr::ui::render {

namespace {

/**
 * @brief Fills the window's party list and appends the control to its foot.
 *
 * The list is re-parsed only when the session's serialised parties or its own
 * party change, so a steady frame allocates nothing.
 */
void prepare_party(const render_input& input,
    model::state::session_handle session, hedgr::ui::widget::foot_state& foot,
    window_ui_state& state) {
    const auto session_party = input.sessions.party_id(session);
    update_party_list(state.parties, input.sessions.parties(session),
        session_party, input.sessions.party_name(session));

    const auto acting = state.parties.index_of(session_party);
    state.acting_party = static_cast<int>(
        acting < state.parties.size() ? acting : 0);
    if (state.parties.size() > 1) {
        foot.controls.push_back(hedgr::ui::widget::foot_combo{
            .id = "acting-as",
            .tooltip = "Act as another party for this session",
            .caption = "Acting as",
            .options = state.parties.names.data(),
            .option_count = static_cast<int>(state.parties.size()),
            .selected = &state.acting_party});
    }
}

}

bool draw_foot_with_party(const render_input& input,
    model::state::session_handle session,
    hedgr::ui::widget::status_bar& status, hedgr::ui::widget::foot_state& foot,
    window_ui_state& state) {
    prepare_party(input, session, foot, state);
    const bool changed
        = hedgr::ui::widget::draw_status_foot(status, input.now_ns, foot);
    if (!changed)
        return false;

    const auto chosen = acted_party(state.parties, state.acting_party,
        input.sessions.party_id(session));
    if (!chosen)
        return true;

    model::command::command request;
    request.kind = model::command::command_kind::select_party;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.party_id = *chosen;
    input.commands.push(std::move(request));
    return true;
}

std::optional<std::string> acted_party(const party_list& parties, int acting,
    std::string_view session_party) {
    if (acting < 0)
        return std::nullopt;
    const auto chosen = static_cast<std::size_t>(acting);
    if (chosen >= parties.size())
        return std::nullopt;
    if (parties.id(chosen) == session_party)
        return std::nullopt;
    return std::string{parties.id(chosen)};
}

}
