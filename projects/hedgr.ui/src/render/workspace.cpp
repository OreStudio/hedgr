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
#include "hedgr.ui/render/workspace.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "internal.hpp"
#include "imgui.h"
#include <cfloat>
#include <cstddef>
#include <format>
#include <string>
#include <variant>

namespace {

/**
 * @brief An icon-only button with a tooltip naming the action.
 *
 * The tooltip is not decoration. An icon without one is a guess, and this
 * interface deliberately has no menu to fall back on.
 */
bool icon_button(const char* id, const char* glyph, const char* tooltip) {
    const float height = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    const bool pressed = ImGui::Button(glyph, ImVec2(height, height));
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

/**
 * @brief The photographic background, drawn under every window.
 *
 * On the background draw list rather than in a window, because that list is
 * emitted before all windows and so cannot be covered by one whatever order
 * the windows are drawn in. The tint darkens the photograph so text reads
 * over it.
 */
void draw_background(const hedgr::ui::render::render_input& input) {
    if (input.background_texture == 0)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddImage(
        static_cast<ImTextureID>(input.background_texture),
        viewport->Pos,
        ImVec2(viewport->Pos.x + viewport->Size.x,
            viewport->Pos.y + viewport->Size.y),
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
        IM_COL32(97, 97, 97, 255));
}

/**
 * @brief The icon strip along the top: the whole of the application's chrome.
 *
 * Add instance, refresh, full screen and exit. Refresh is deliberately dumb:
 * it re-requests every open window, and the runtime decides what actually
 * needs fetching.
 */
void draw_toolbar(const hedgr::ui::render::render_input& input) {
    namespace icon = hedgr::ui::icon;
    using hedgr::model::command::command_kind;
    namespace detail = hedgr::ui::render::detail;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##hedgr-toolbar", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (icon_button("add", icon::add, "Add instance"))
        detail::add_instance_requested = true;

    ImGui::SameLine();
    if (icon_button("refresh", icon::refresh, "Refresh every window")) {
        /*
         * One command per open window. The interface does not know how to
         * fetch anything: it names the windows that want fresh data and lets
         * the runtime decide when and whether that happens.
         */
        const auto& layout = input.windows.layout();
        const auto open = input.windows.open_column();
        for (std::size_t index = 0; index < layout.alive.size(); ++index) {
            if (layout.alive[index] == 0 || open[index] == 0)
                continue;
            hedgr::model::command::command request;
            request.kind = command_kind::refresh_window;
            request.window_id = std::string{input.windows.id(
                hedgr::model::state::window_handle{
                    static_cast<std::uint32_t>(index), layout.generation[index]})};
            input.commands.push(std::move(request));
        }
    }

    ImGui::SameLine();
    if (icon_button("fullscreen", icon::fullscreen, "Toggle full screen")) {
        /*
         * Full screen is a property of the GLFW window, which this library
         * does not own and must not reach for. It synthesises the shortcut the
         * application already listens for during its frame loop, so the
         * decision about how to change it stays in the layer that has the
         * window handle.
         */
        ImGui::GetIO().AddKeyEvent(ImGuiKey_F11, true);
    }

    ImGui::SameLine();
    if (icon_button("exit", icon::exit_app, "Exit Hedgr")) {
        hedgr::model::command::command request;
        request.kind = command_kind::shutdown;
        input.commands.push(std::move(request));
    }

    ImGui::End();
}

/**
 * @brief The visible name of a window: its kind and, where it has one, its
 * subject.
 *
 * Titles are read from the window's state, which is cold. Only an open window
 * that survived the culling gates reaches here, so the read never lands on the
 * per-frame path for a window nobody is looking at.
 */
std::string window_title(hedgr::model::domain::window_kind kind,
    const hedgr::model::domain::window_state& state) {
    using hedgr::model::domain::window_kind;
    switch (kind) {
    case window_kind::chart: {
        const auto& chart = std::get<hedgr::model::domain::chart_state>(state);
        return chart.label.empty() ? std::string{"Chart"} : chart.label;
    }
    case window_kind::grid: {
        const auto& grid = std::get<hedgr::model::domain::grid_state>(state);
        return grid.resource.empty() ? std::string{"Grid"} : grid.resource;
    }
    case window_kind::notice: {
        const auto& notice = std::get<hedgr::model::domain::notice_state>(state);
        return notice.title.empty() ? std::string{"Notice"} : notice.title;
    }
    case window_kind::instance_manager:
        return "Instance Manager";
    }
    return "Window";
}

}

namespace hedgr::ui::render {

workspace::workspace() = default;

workspace::~workspace() = default;

void workspace::render(const render_input& input) {
    draw_toolbar(input);
    draw_background(input);

    const auto& layout = input.windows.layout();

    /*
     * Windows are walked in slot order and the hot columns are read directly,
     * rather than through the per-row accessors. The columns are contiguous
     * arrays, so this is a stride-1 walk: one cache line per window decides
     * whether that window costs anything at all.
     */
    const auto kind_column = input.windows.kind_column();
    const auto open_column = input.windows.open_column();
    const auto refresh_column = input.windows.next_refresh_column();

    for (std::size_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;

        const auto kind =
            static_cast<hedgr::model::domain::window_kind>(kind_column[index]);

        /*
         * The open flag culls every window except the Instance Manager, which
         * is root: closing it would leave an interface with no way to open
         * anything again, so it is drawn whether or not the flag is set. The
         * runtime still owns the flag and may suppress the row entirely.
         */
        if (open_column[index] == 0
            && kind != hedgr::model::domain::window_kind::instance_manager)
            continue;

        const hedgr::model::state::window_handle handle{
            static_cast<std::uint32_t>(index),
            layout.generation[index]};

        /*
         * The deadline is an input, not an order: the runtime owns the column
         * and decides whether the request is worth acting on. The interface
         * only says that by its own clock the data is now stale.
         */
        if (input.now_ns >= refresh_column[index]) {
            hedgr::model::command::command request;
            request.kind = hedgr::model::command::command_kind::refresh_window;
            request.window_id = std::string{input.windows.id(handle)};
            input.commands.push(std::move(request));
        }

        const std::string title = window_title(kind, input.windows.state(handle));

        /*
         * The visible title can change with the data, so the ImGui identity
         * is pinned to the stable id after the ### marker. Without it a
         * retitled window would be a different window to ImGui, and would
         * lose its position, size and collapsed state.
         */
        const std::string begin_label = std::format("{}  {}###{}",
            hedgr::ui::theme::window_kind_icon(kind), title,
            input.windows.id(handle));

        ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 220.0f),
            ImVec2(FLT_MAX, FLT_MAX));

        if (!ImGui::Begin(begin_label.c_str())) {
            /*
             * Collapsed or fully clipped: ImGui still wants its End, and
             * nothing inside the window is submitted. This is the content
             * culling that keeps a workspace of hundreds of windows cheap.
             */
            ImGui::End();
            continue;
        }

        const auto key = std::string{input.windows.id(handle)};
        window_ui_state& state = ui_state_[key];
        if (const auto draw = renderer_for(kind))
            draw(handle, input, state);

        ImGui::End();
    }
}

}
