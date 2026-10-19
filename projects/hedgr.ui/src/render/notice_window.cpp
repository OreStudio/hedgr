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
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <cstddef>
#include <string>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::state::window_handle;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

}

namespace hedgr::ui::render {

/**
 * @brief A notice window: something a person has to read.
 *
 * The order is the whole point of the surface. The headline and the plain
 * message come first, because that is what the person needs; the low-level
 * detail comes last and dimmed, because it is what an administrator will ask
 * for and noise to everybody else. This is the shape the "instance is down,
 * contact your system administrator" case renders in.
 */
void render_notice(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(status);
    static_cast<void>(lock);
    using hedgr::model::domain::notice_state;

    const auto& notice = std::get<notice_state>(input.windows.state(handle));

    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(
            hedgr::ui::theme::status_colour(
                hedgr::model::domain::session_status::failed)));
    ImGui::TextUnformatted(hedgr::ui::icon::error);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (notice.title.empty())
        ImGui::TextUnformatted("Notice");
    else
        ImGui::TextUnformatted(notice.title.c_str());

    ImGui::Separator();

    if (notice.text.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("There is nothing to report.");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextWrapped("%s", notice.text.c_str());
    }

    if (!notice.detail.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Details: %s", notice.detail.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(hedgr::ui::icon::cancel)) {
        command request;
        request.kind = command_kind::close_window;
        request.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(request));
    }
    ImGui::SetItemTooltip("Close this notice");

    ImGui::SameLine();
    if (ImGui::Button(hedgr::ui::icon::information))
        state.show_detail = !state.show_detail;
    ImGui::SetItemTooltip("Show how this notice was raised");
    if (state.show_detail) {
        ImGui::Text("window %s", std::string{input.windows.id(handle)}.c_str());
    }
}

}
