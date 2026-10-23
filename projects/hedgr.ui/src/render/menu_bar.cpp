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
#include "hedgr.ui/render/menu_bar.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ores.platform/process/executable.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

namespace {

namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::model::domain::store_lock;

/** @brief The top-level menus' places in the bar, which stay fixed. */
constexpr int file_entry = 0;
constexpr int view_entry = 1;
constexpr int tools_entry = 2;
constexpr int help_entry = 3;

constexpr const char* logo_texture_id = "ore-logo-white";
constexpr const char* about_title = "About ORE Studio###hedgr-about";
constexpr float about_logo_side = 72.0f;

[[nodiscard]] ImU32 u32(ImVec4 colour) {
    return ImGui::ColorConvertFloat4ToU32(colour);
}

/** @brief The swirl as a bar-high brand mark: not a button, with a name on hover. */
void draw_logo(const render_input& input, std::string_view logo_svg) {
    const float side = theme::menu_bar_height;
    ImGui::Dummy(ImVec2(side, side));
    ImGui::SetItemTooltip("ORE Studio");
    if (logo_svg.empty() || input.textures == nullptr)
        return;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float pad = theme::menu_bar_logo_padding;
    const float scale = std::max(1.0f,
        ImGui::GetIO().DisplayFramebufferScale.y);
    const int pixels = static_cast<int>(std::lround((side - 2.0f * pad) * scale));
    const ImTextureID texture = input.textures->svg(logo_texture_id, logo_svg,
        pixels, pixels);
    if (texture != 0)
        ImGui::GetWindowDrawList()->AddImage(texture,
            ImVec2(min.x + pad, min.y + pad), ImVec2(max.x - pad, max.y - pad),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            ImGui::GetColorU32(ImGuiCol_Text));
}

/**
 * @brief The session the TOOLS rows act on: the first one signed in, or none.
 *
 * Series browsers and the command centre belong to a session, and the bar has
 * no notion of a chosen one yet.
 */
[[nodiscard]] std::optional<hedgr::model::state::session_handle>
current_session(const render_input& input) {
    const auto& layout = input.sessions.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        const hedgr::model::state::session_handle session{index,
            layout.generation[index]};
        if (input.sessions.status(session)
            == hedgr::model::domain::session_status::authenticated)
            return session;
    }
    return std::nullopt;
}

/** @brief Asks the runtime for a window of @p kind bound to @p session. */
void request_session_window(const render_input& input,
    hedgr::model::state::session_handle session,
    hedgr::model::domain::window_kind kind) {
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::open_window;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.new_window_kind = kind;
    input.commands.push(std::move(request));
}

/**
 * @brief FILE: Instances, the store's lock rows, then Quit.
 *
 * While the store is shut the catalogue is behind the master password, so the
 * menu offers the way in and Quit, as the shortcuts bar draws nothing then.
 */
void draw_file_menu(const render_input& input, menu_bar_result& result) {
    if (shows_instances(input)) {
        if (widget::menu_item_row({.label = "Instances"})) {
            request_instances_window(input);
            result.focus_instances = true;
        }
        widget::menu_separator();
    }
    switch (input.store_lock) {
    case store_lock::unlocked:
        if (widget::menu_item_row({.label = "Lock store"}))
            result.lock_requested = true;
        break;
    case store_lock::absent:
        if (widget::menu_item_row({.label = "Create store"}))
            result.unlock_requested = true;
        break;
    case store_lock::locked:
    case store_lock::failed:
        if (widget::menu_item_row({.label = "Unlock store"}))
            result.unlock_requested = true;
        break;
    }
    widget::menu_separator();
    if (widget::menu_item_row({.label = "Quit"}))
        result.quit_requested = true;
}

/** @brief VIEW: Undock all, live only while something is docked. */
void draw_view_menu(bool desk_docked, menu_bar_result& result) {
    if (widget::menu_item_row({.label = "Undock all", .enabled = desk_docked}))
        result.undock_all = true;
}

/** @brief TOOLS: the windows that belong to a signed-in session. */
void draw_tools_menu(const render_input& input) {
    namespace domain = hedgr::model::domain;
    const auto session = current_session(input);
    if (widget::menu_item_row({.label = "Series browser",
            .enabled = session.has_value()}))
        request_session_window(input, *session,
            domain::window_kind::series_browser);
    if (widget::menu_item_row({.label = "Accounts",
            .enabled = session.has_value()}))
        request_accounts_window(input, input.sessions.instance_id(*session));
    const bool admin = session
        && input.sessions.mode(*session)
            == domain::session_mode::system_administration;
    if (admin && widget::menu_item_row({.label = "Command centre"}))
        request_session_window(input, *session,
            domain::window_kind::command_centre);
}

/** @brief The About window: the mark, the name, the version and the licence. */
void draw_about(const render_input& input, std::string_view logo_svg,
    widget::menu_bar_state& state) {
    if (!state.about_open)
        return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(about_title, &state.about_open, flags)) {
        if (!logo_svg.empty() && input.textures != nullptr) {
            const float scale = std::max(1.0f,
                ImGui::GetIO().DisplayFramebufferScale.y);
            const int pixels = static_cast<int>(
                std::lround(about_logo_side * scale));
            const ImTextureID texture = input.textures->svg(logo_texture_id,
                logo_svg, pixels, pixels);
            if (texture != 0)
                ImGui::Image(texture, ImVec2(about_logo_side, about_logo_side));
        }
        ImGui::TextUnformatted("ORE Studio");
        ImGui::TextDisabled("Version %.*s",
            static_cast<int>(version_text().size()), version_text().data());
        ImGui::TextDisabled("Licensed under the GNU GPL v3");
    }
    ImGui::End();
}

}

std::string_view version_text() noexcept {
#ifdef HEDGR_VERSION
    return HEDGR_VERSION;
#else
    return "unknown";
#endif
}

std::filesystem::path logo_svg_path() {
    constexpr const char* name = "ore-logo-white.svg";
    const auto self = ores::platform::process::executable_path();
    std::vector<std::filesystem::path> candidates;
    if (!self.empty()) {
        const auto exe_dir = self.parent_path();
        candidates.push_back(exe_dir.parent_path() / "share" / "hedgr.ui"
            / "branding" / name);
        candidates.push_back(exe_dir / "branding" / name);
    }
    candidates.push_back(std::filesystem::path("assets/branding") / name);

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

void draw_menu_bar(const render_input& input, std::string_view logo_svg,
    bool desk_docked, widget::menu_bar_state& state, menu_bar_result& result) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        ImVec2(theme::menu_bar_margin, theme::menu_bar_margin));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::BeginViewportSideBar("###hedgr-menu-bar",
        ImGui::GetMainViewport(), ImGuiDir_Up, theme::menu_bar_extent, flags);
    ImGui::PopStyleVar(2);
    if (visible) {
        /*
         * The window has no fill of its own: the plate is drawn here, translucent,
         * over the wallpaper that the workspace put on the background list.
         */
        const ImVec2 at = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const ImVec2 plate_min(at.x + theme::menu_bar_margin,
            at.y + theme::menu_bar_margin);
        const ImVec2 plate_max(at.x + size.x - theme::menu_bar_margin,
            at.y + size.y - theme::menu_bar_margin);
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        theme::fill_stops(draw, plate_min, plate_max, theme::menu_bar_stops,
            theme::menu_bar_rounding);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        draw_logo(input, logo_svg);

        const float height = theme::menu_bar_height;
        ImGui::SameLine();
        widget::menu_bar_label(state, file_entry, "File",
            [&] { draw_file_menu(input, result); }, height);
        if (shows_instances(input)) {
            ImGui::SameLine();
            widget::menu_bar_label(state, view_entry, "View",
                [&] { draw_view_menu(desk_docked, result); }, height);
            ImGui::SameLine();
            widget::menu_bar_label(state, tools_entry, "Tools",
                [&] { draw_tools_menu(input); }, height);
            ImGui::SameLine();
            widget::menu_bar_label(state, help_entry, "Help", [&] {
                if (widget::menu_item_row({.label = "About"}))
                    state.about_open = true;
            }, height);
        }

        /* The hairline round the plate, drawn over the fill. */
        draw->AddRect(ImVec2(plate_min.x + 0.5f, plate_min.y + 0.5f),
            ImVec2(plate_max.x - 0.5f, plate_max.y - 0.5f),
            u32(theme::menu_bar_edge()), theme::menu_bar_rounding);
        ImGui::PopStyleVar();
    }
    ImGui::End();
    draw_about(input, logo_svg, state);
}

}
