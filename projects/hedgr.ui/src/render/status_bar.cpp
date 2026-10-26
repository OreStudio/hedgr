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
#include "hedgr.ui/render/status_bar.hpp"

#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdint>
#include <string_view>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;
namespace theme = hedgr::ui::theme;

constexpr const char* status_bar_window = "###hedgr-status-bar";

/** @brief How the connection end reads, and the colour it reads in. */
struct connection_state final {
    const char* word = "";
    ImU32 colour = 0;
};

[[nodiscard]] ImU32 u32(ImVec4 colour) {
    return ImGui::ColorConvertFloat4ToU32(colour);
}

/**
 * @brief Summarises every live session into one word.
 *
 * A failure outranks a connection in flight, which outranks a quiet set of
 * connected sessions: a wall with one broken login has to say so even while
 * three others work. The count only appears once there is more than one
 * session, because "1 connected" is a longer way of saying "connected".
 */
[[nodiscard]] connection_state connection_of(const render_input& input) {
    const auto& layout = input.sessions.layout();
    std::size_t total = 0;
    std::size_t connected = 0;
    bool failed = false;
    bool connecting = false;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        ++total;
        const hedgr::model::state::session_handle handle{index,
            layout.generation[index]};
        switch (input.sessions.status(handle)) {
        case domain::session_status::authenticated:
            ++connected;
            break;
        case domain::session_status::connecting:
            connecting = true;
            break;
        case domain::session_status::failed:
            failed = true;
            break;
        case domain::session_status::disconnected:
            break;
        }
    }

    if (total == 0)
        return {"no session", u32(theme::dim_text())};
    if (failed)
        return {"failed", theme::failure_colour};
    if (connecting)
        return {"connecting", theme::warning_colour};
    if (connected == total)
        return {"connected", theme::status_colour(
            domain::session_status::authenticated)};
    if (connected > 0)
        return {"partly connected", theme::warning_colour};
    return {"disconnected", u32(theme::dim_text())};
}

/** @brief The colour the message ladder's current line is drawn in. */
[[nodiscard]] ImU32 message_colour(
    hedgr::ui::widget::status_severity severity) noexcept {
    switch (severity) {
    case hedgr::ui::widget::status_severity::notice:
        return u32(theme::dim_text());
    case hedgr::ui::widget::status_severity::problem:
        return theme::warning_colour;
    case hedgr::ui::widget::status_severity::error:
        return theme::failure_colour;
    }
    return u32(theme::dim_text());
}

}

void draw_status_bar(const render_input& input,
    hedgr::ui::widget::status_bar& status) {
    status.advance(input.now_ns);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::BeginViewportSideBar(status_bar_window,
        ImGui::GetMainViewport(), ImGuiDir_Down, theme::status_bar_height,
        flags);
    ImGui::PopStyleVar(2);
    if (visible) {
        const ImVec2 at = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
            u32(theme::status_bar_surface()));
        draw->AddLine(at, ImVec2(at.x + size.x, at.y),
            u32(theme::status_bar_edge()));

        const float line = ImGui::GetTextLineHeight();
        const float text_y = at.y + (size.y - line) * 0.5f;
        const float padding = theme::status_bar_padding_x;

        const auto connection = connection_of(input);
        const float dot = theme::instances_status_dot;
        const float dot_gap = padding;
        const float connection_width = 2.0f * dot + dot_gap
            + ImGui::CalcTextSize(connection.word).x;
        const float connection_left = at.x + size.x - padding
            - connection_width;
        draw->AddCircleFilled(
            ImVec2(connection_left + dot, at.y + size.y * 0.5f), dot,
            connection.colour);
        draw->AddText(
            ImVec2(connection_left + 2.0f * dot + dot_gap, text_y),
            connection.colour, connection.word);

        const std::string_view message = status.message();
        if (!message.empty()) {
            ImVec4 colour = ImGui::ColorConvertU32ToFloat4(
                message_colour(status.severity()));
            colour.w *= status.alpha(input.now_ns);
            const float message_right = connection_left - 2.0f * padding;
            draw->PushClipRect(ImVec2(at.x + padding, at.y),
                ImVec2(message_right, at.y + size.y), true);
            draw->AddText(ImVec2(at.x + padding, text_y),
                ImGui::ColorConvertFloat4ToU32(colour), message.data(),
                message.data() + message.size());
            draw->PopClipRect();
            if (ImGui::IsWindowHovered()
                && ImGui::GetIO().MousePos.x < message_right)
                ImGui::SetTooltip("%s", message.data());
        }
    }
    ImGui::End();
}

}
