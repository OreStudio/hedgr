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
#include <format>
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
    state.choice = 0;
    state.seeded = false;
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
        state.choice = 0;
        state.seeded = false;
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

    /*
     * The list starts on the party the deployment chose, because that is the
     * honest starting point: the operator sees what the session would act as if
     * they said nothing, and moves it one row.
     */
    if (!state.seeded) {
        const auto acting = state.parties.index_of(session_party);
        state.choice = acting < state.parties.size()
            ? static_cast<int>(acting) : 0;
        state.seeded = true;
    }
    if (state.choice < 0 || static_cast<std::size_t>(state.choice)
            >= state.parties.size())
        state.choice = 0;

    const ImVec2 size(480.0f, 250.0f);
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
    const char* title = "Act as a party###hedgr-party";
    if (!ImGui::Begin(title, &open, flags)) {
        ImGui::End();
        if (!open)
            dismiss(state);
        return;
    }
    hedgr::ui::theme::shade_window_background(
        hedgr::ui::theme::window_gradient);

    const std::string acting_label =
        input.sessions.party_name(*session).empty()
        ? std::string{session_party}
        : std::string{input.sessions.party_name(*session)};
    ImGui::TextWrapped(
        "This login may act as %zu parties. It was signed in as %s.",
        state.parties.size(), acting_label.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The party a session acts as decides which data every window reads. "
        "Choose it once here; it is remembered for this account, so the next "
        "sign-in lands in it.");

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Act as");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##party", state.parties.name(
            static_cast<std::size_t>(state.choice)))) {
        for (std::size_t i = 0; i < state.parties.size(); ++i) {
            const bool chosen = static_cast<int>(i) == state.choice;
            if (ImGui::Selectable(state.parties.name(i), chosen))
                state.choice = static_cast<int>(i);
            if (chosen)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    if (ImGui::Button(std::format("Act as {}",
            state.parties.name(static_cast<std::size_t>(state.choice))).c_str(),
            ImVec2(220.0f, 0.0f))) {
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::select_party;
        request.instance_id = state.instance_id;
        request.account_id = state.account_id;
        request.party_id = state.parties.id(
            static_cast<std::size_t>(state.choice));
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Sign-in for account '" << state.account_id
            << "' asked to act as party '" << request.party_id << "' ("
            << state.parties.name(static_cast<std::size_t>(state.choice))
            << ")";
        input.commands.push(std::move(request));
        forget(state);
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now", ImVec2(120.0f, 0.0f)))
        dismiss(state);
    ImGui::SetItemTooltip(
        "Act as the party the deployment signed this login in as. The party can "
        "be changed later from a window's status bar.");

    ImGui::Spacing();
    ImGui::TextDisabled(
        "Change it later from the market browser's status bar. Windows already "
        "open read the new party's data once the change is confirmed.");

    ImGui::End();

    /* The title bar's close button is the second way to decline. */
    if (!open)
        dismiss(state);
}

}
