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
#include "hedgr.ui/render/party_prompt.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;

/** @brief This file's logger, which says which party a session chose. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.party_prompt");
    return instance;
}

/** @brief The one string that names a session across frames. */
[[nodiscard]] std::string key_of(std::string_view instance_id,
    std::string_view account_id) {
    std::string key{instance_id};
    key.push_back('\x1f');
    key += account_id;
    return key;
}

void forget(party_prompt_state& state) {
    state.instance_id.clear();
    state.account_id.clear();
    state.parties = party_list{};
}

/** @brief Takes up the first signed-in session that still has to be asked. */
void offer_to_first(const render_input& input, party_prompt_state& state) {
    const auto& layout = input.sessions.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        const model::state::session_handle session{index,
            layout.generation[index]};
        if (!needs_party_choice(input, session))
            continue;
        const auto instance_id = input.sessions.instance_id(session);
        const auto account_id = input.sessions.account_id(session);
        if (state.dismissed.contains(key_of(instance_id, account_id)))
            continue;

        state.instance_id = std::string{instance_id};
        state.account_id = std::string{account_id};
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Asking account '" << account_id << "' on '" << instance_id
            << "' which party to act as: the login holds "
            << model::event::party_count(input.sessions.parties(session))
            << " and none is remembered";
        return;
    }
}

/** @brief Puts the prompt down for the run, remembering which session it was. */
void dismiss(party_prompt_state& state) {
    if (!state.instance_id.empty())
        state.dismissed.insert(key_of(state.instance_id, state.account_id));
    forget(state);
}

}

void draw_party_prompt(const render_input& input, party_prompt_state& state) {
    if (state.instance_id.empty())
        offer_to_first(input, state);
    if (state.instance_id.empty())
        return;

    /*
     * The session may have answered elsewhere - the market browser's foot closes
     * this question too - or gone, so it is found again and re-asked every
     * frame rather than trusted from when the prompt opened.
     */
    const auto session = input.sessions.find(state.instance_id, state.account_id);
    if (!session || !needs_party_choice(input, *session)) {
        forget(state);
        return;
    }

    const auto session_party = input.sessions.party_id(*session);
    update_party_list(state.parties, input.sessions.parties(*session),
        session_party, input.sessions.party_name(*session));
    if (state.parties.empty()) {
        forget(state);
        return;
    }

    const auto session_party_index = state.parties.index_of(session_party);

    /*
     * One row per party, and the row is the answer. There is no combo to open
     * and no confirm button, because the question has one answer and the row
     * that is clicked is it: a two-step pick-then-confirm would ask the operator
     * to say the same thing twice, and a "Not now" would be a button that means
     * "act as the default", which is the accident this prompt exists to
     * prevent.
     */
    const auto rows = static_cast<int>(state.parties.size());
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    const float list_height = std::min(row_height * static_cast<float>(rows),
        row_height * 9.0f);
    /*
     * The title bar, the window's padding and the gap above the list, added to
     * the list itself: a window sized to its contents exactly leaves no empty
     * band under the last row.
     */
    const ImVec2 size(420.0f, list_height
            + ImGui::GetTextLineHeightWithSpacing()
            + 2.0f * ImGui::GetStyle().WindowPadding.y + 8.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) / 2.0f,
            viewport->Pos.y + (viewport->Size.y - size.y) / 2.0f),
        ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoDocking;

    bool open = true;
    const char* title = "Act as which party###hedgr-party";
    if (!ImGui::Begin(title, &open, flags)) {
        ImGui::End();
        if (!open)
            dismiss(state);
        return;
    }
    hedgr::ui::theme::shade_window_background(
        hedgr::ui::theme::window_gradient);

    std::optional<std::size_t> chosen;
    if (ImGui::BeginChild("##parties", ImVec2(0.0f, list_height),
            ImGuiChildFlags_None)) {
        for (std::size_t i = 0; i < state.parties.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool acting = i == session_party_index;
            if (ImGui::Selectable(state.parties.name(i), acting))
                chosen = i;
            if (acting) {
                ImGui::SameLine();
                ImGui::TextDisabled("signed in as");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SetItemTooltip(
        "The party this session acts as, which decides the data every window "
        "reads. Remembered for this account.");

    ImGui::End();

    if (chosen) {
        auto request = party_choice_command(state, *chosen);
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Sign-in for account '" << state.account_id
            << "' asked to act as party '" << request.party_id << "' ("
            << state.parties.name(*chosen) << ")";
        input.commands.push(std::move(request));
        forget(state);
        return;
    }

    /* The title bar's close button is the only way to decline, and it defers. */
    if (!open)
        dismiss(state);
}

hedgr::model::command::command party_choice_command(
    const party_prompt_state& state, std::size_t index) {
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::select_party;
    request.instance_id = state.instance_id;
    request.account_id = state.account_id;
    request.party_id = index < state.parties.size()
        ? std::string{state.parties.id(index)} : std::string{};
    return request;
}

}
