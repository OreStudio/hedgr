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
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/instances_panel.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ores.platform/process/executable.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <format>
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
namespace domain = hedgr::model::domain;
using hedgr::model::domain::store_lock;

/** @brief The top-level menus' places in the bar, which stay fixed. */
constexpr int file_entry = 0;
constexpr int view_entry = 1;
constexpr int instances_entry = 2;
constexpr int accounts_entry = 3;
constexpr int tools_entry = 4;
constexpr int help_entry = 5;

constexpr const char* logo_texture_id = "ore-logo-white";
constexpr const char* windows_submenu_id = "##view-windows";
constexpr const char* accounts_of_id = "##accounts-of";
constexpr const char* sign_in_popup_id = "Sign in###hedgr-menu-sign-in";
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
 * @brief FILE: Instances, the store's lock rows, then Quit.
 *
 * While the store is shut the catalogue is behind the master password, so the
 * menu offers the way in and Quit, and the other menus are not drawn.
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

/** @brief Whether any window is open, so the Windows row has something to list. */
[[nodiscard]] bool any_window_open(const render_input& input) {
    const auto open = input.windows.open_column();
    for (std::size_t index = 0; index < open.size(); ++index)
        if (open[index] != 0)
            return true;
    return false;
}

/**
 * @brief Draws one open window's row: the title raises it, the x closes it.
 *
 * The row is two items so that raising and closing are separate targets: a
 * click anywhere on the title raises, and the small x at the right closes
 * through the command a window's own close control pushes. Returns whether
 * either happened, so the panel closes on the click.
 */
bool draw_window_row(const render_input& input, menu_bar_result& result,
    std::string_view id, std::string_view title) {
    const float height = ImGui::GetTextLineHeight()
        + 2.0f * theme::menu_row_padding_y;
    const float close_width = ImGui::GetFrameHeight();
    const float gap = theme::menu_row_gap;
    const float room = ImGui::GetContentRegionAvail().x;
    const float title_width = room - close_width - gap;

    ImGui::PushID(id.data(), id.data() + id.size());
    const bool raised = ImGui::Selectable("##raise", false,
        ImGuiSelectableFlags_None, ImVec2(std::max(1.0f, title_width), height));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(min.x + theme::menu_row_padding_x, min.y),
        ImVec2(max.x - theme::menu_row_padding_x, max.y), true);
    draw->AddText(
        ImVec2(min.x + theme::menu_row_padding_x,
            min.y + theme::menu_row_padding_y),
        ImGui::GetColorU32(ImGuiCol_Text), title.data(),
        title.data() + title.size());
    draw->PopClipRect();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bring the window to the front");
    if (raised)
        result.focus_window = std::string{id};

    ImGui::SameLine(0.0f, gap);
    const bool closed = ImGui::Button("x", ImVec2(close_width, height));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Close the window");
    ImGui::PopID();

    if (closed) {
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::close_window;
        request.window_id = std::string{id};
        input.commands.push(std::move(request));
    }
    return raised || closed;
}

/**
 * @brief The VIEW > Windows panel: every open window under its kind's heading.
 *
 * The kinds are walked in the enum's order, so the grouping is stable and the
 * frame allocates nothing for a list. A heading is drawn only for a kind that
 * has an open window. The last row closes every mini plot, which is the one
 * action those cards have as a set, and it is left out when there is none.
 */
void draw_windows_panel(const render_input& input, menu_bar_result& result) {
    const ImVec2 anchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y);
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const float width = theme::windows_panel_width;
    const float margin = theme::menu_padding;
    const float max_x = viewport->WorkPos.x + viewport->WorkSize.x - margin;
    const float left = std::min(anchor.x, max_x - width);
    ImGui::SetNextWindowPos(
        ImVec2(std::max(left, viewport->WorkPos.x), anchor.y));
    const widget::menu_popup panel(windows_submenu_id, width, std::nullopt,
        theme::windows_panel_height);
    if (!panel)
        return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool acted = false;
    if (ImGui::BeginChild("##windows-list", ImVec2(0.0f, 0.0f))) {
        const auto& layout = input.windows.layout();
        const auto kinds = input.windows.kind_column();
        const auto open = input.windows.open_column();
        std::string title;
        std::size_t mini_plots = 0;
        for (std::size_t kind = 0; kind < domain::window_kind_count; ++kind) {
            bool heading = false;
            for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
                if (!layout.is_alive(index) || open[index] == 0
                    || kinds[index] != kind)
                    continue;
                const hedgr::model::state::window_handle handle{index,
                    layout.generation[index]};
                title.clear();
                const auto kind_value = static_cast<domain::window_kind>(kind);
                append_window_title(title, kind_value,
                    input.windows.state(handle), input);
                if (!heading) {
                    const auto kind_text = domain::to_string(kind_value);
                    char text[32];
                    std::snprintf(text, sizeof text, "%.*s",
                        static_cast<int>(kind_text.size()), kind_text.data());
                    widget::menu_heading(text);
                    heading = true;
                }
                acted = draw_window_row(input, result,
                            input.windows.id(handle), title)
                    || acted;
                if (kind == static_cast<std::size_t>(
                        domain::window_kind::mini_plot))
                    ++mini_plots;
            }
        }

        if (mini_plots > 0) {
            widget::menu_separator();
            if (widget::menu_item_row({.label = "Close all mini plots"})) {
                for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
                    if (!layout.is_alive(index) || open[index] == 0
                        || kinds[index] != static_cast<std::uint8_t>(
                            domain::window_kind::mini_plot))
                        continue;
                    const hedgr::model::state::window_handle handle{index,
                        layout.generation[index]};
                    hedgr::model::command::command request;
                    request.kind =
                        hedgr::model::command::command_kind::close_window;
                    request.window_id = std::string{input.windows.id(handle)};
                    input.commands.push(std::move(request));
                }
                acted = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (acted) {
        /*
         * A pick closes the panel and the menu it hangs off, as a menu row
         * does. CloseCurrentPopup would only close this panel, leaving VIEW
         * open over an empty desk, so the whole popup stack goes.
         */
        ImGui::ClosePopupToLevel(0, true);
    }
}

/**
 * @brief VIEW: Undock all, live only while something is docked, Full screen,
 * the Toolbar row, checked while the floating toolbar is shown, and Windows.
 *
 * The switches carry a check rather than the chosen-row bar, so a menu of
 * toggles reads as state and not as navigation. Every row reserves the check
 * column so their labels stay on one line.
 */
void draw_view_menu(const render_input& input, bool desk_docked,
    menu_bar_result& result) {
    if (widget::menu_item_row({.label = "Undock all", .checkable = true,
            .enabled = desk_docked}))
        result.undock_all = true;
    if (widget::menu_item_row({.label = "Full screen", .shortcut = "F11",
            .checkable = true, .checked = input.fullscreen}))
        result.fullscreen_toggled = true;
    auto& visible = input.market_view.toolbar_visible;
    if (widget::menu_item_row({.label = "Toolbar", .checkable = true,
            .checked = visible}))
        visible = !visible;
    auto& reference = input.market_view.show_reference_providers;
    if (widget::menu_item_row({.label = "Reference providers",
            .checkable = true, .checked = reference}))
        reference = !reference;

    const bool any = any_window_open(input);
    const bool pressed = widget::menu_item_row({.label = "Windows",
        .submenu = true, .checkable = true, .enabled = any});
    if (!any)
        return;
    if ((pressed || ImGui::IsItemHovered())
        && !ImGui::IsPopupOpen(windows_submenu_id))
        ImGui::OpenPopup(windows_submenu_id);
    draw_windows_panel(input, result);
}

namespace {

/** @brief Whether @p lhs sorts before @p rhs, folding ASCII case. */
[[nodiscard]] bool folded_less(std::string_view lhs,
    std::string_view rhs) noexcept {
    const std::size_t shared = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
    for (std::size_t i = 0; i < shared; ++i) {
        const char a = lhs[i] >= 'A' && lhs[i] <= 'Z'
            ? static_cast<char>(lhs[i] - 'A' + 'a') : lhs[i];
        const char b = rhs[i] >= 'A' && rhs[i] <= 'Z'
            ? static_cast<char>(rhs[i] - 'A' + 'a') : rhs[i];
        if (a != b)
            return a < b;
    }
    return lhs.size() < rhs.size();
}

/** @brief The name an account is read by in the menu: its name, or principal. */
[[nodiscard]] std::string_view account_label(const domain::account& row) {
    return row.name.empty() ? std::string_view{row.principal}
                            : std::string_view{row.name};
}

/**
 * @brief Fills @p menu with the instances that have at least one account.
 *
 * A walk over both slabs, because an environment with no login is not a place
 * this menu can take the operator and listing it would be a row that asks to be
 * clicked and has nothing under it. The vector is kept across frames, so a
 * steady frame reuses its room.
 */
void collect_environments(const render_input& input,
    widget::accounts_menu_state& menu) {
    menu.environments.clear();
    const auto& layout = input.instances.layout();
    const auto rows = input.instances.rows();
    const auto& accounts_layout = input.accounts.layout();
    const auto account_rows = input.accounts.rows();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        for (std::uint32_t account = 0;
             account < accounts_layout.capacity(); ++account) {
            if (accounts_layout.is_alive(account)
                && account_rows[account].instance_id == rows[index].id) {
                menu.environments.push_back(index);
                break;
            }
        }
    }
    std::sort(menu.environments.begin(), menu.environments.end(),
        [&](std::uint32_t lhs, std::uint32_t rhs) {
            return widget::instance_less(rows[lhs].name, rows[lhs].id,
                rows[rhs].name, rows[rhs].id);
        });
}

/** @brief How many accounts one instance has, for the row's trailing count. */
[[nodiscard]] std::size_t account_count(const render_input& input,
    std::string_view instance_id) {
    const auto& layout = input.accounts.layout();
    const auto rows = input.accounts.rows();
    std::size_t count = 0;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index)
        if (layout.is_alive(index) && rows[index].instance_id == instance_id)
            ++count;
    return count;
}

/**
 * @brief Signs in as @p row, or asks for its password when none is memorised.
 *
 * The credential is the store's when it holds one and the store is open; then
 * the command carries no password and the runtime reads the stored secret. A
 * typed password travels with the command and never enters a table.
 */
void sign_in_to(const render_input& input, widget::menu_bar_state& state,
    const domain::account& row, std::string_view environment) {
    if (sign_in_needs_password(input.store_lock, row.has_credential)) {
        auto& form = state.sign_in;
        form.instance_id = row.instance_id;
        form.instance_name = std::string{environment};
        form.account_id = row.id;
        form.principal = row.principal;
        form.password.fill('\0');
        form.remember = false;
        form.focus = true;
        form.open = true;
        return;
    }

    input.commands.push(account_sign_in_command(row.instance_id, row.id,
        row.principal));
    raise_shortcut_activated(input, std::format("Sign in to {} as {}",
        environment, row.principal));
}

/** @brief One account's row; returns whether it started a sign-in. */
bool draw_account_row(const render_input& input, widget::menu_bar_state& state,
    const domain::account& row, std::string_view environment) {
    const char* label = row.name.empty() ? row.principal.c_str()
                                         : row.name.c_str();
    if (widget::menu_item_row({.label = label})) {
        sign_in_to(input, state, row, environment);
        return true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s%s", static_cast<int>(row.principal.size()),
            row.principal.data(),
            row.has_credential ? "\nSigns in with the saved password"
                               : "\nAsks for the password");
    }
    return false;
}

/** @brief Draws every account of one environment, under its folders' names. */
bool draw_environment_accounts(const render_input& input,
    widget::menu_bar_state& state, std::string_view instance_id,
    std::string_view environment) {
    auto& menu = state.accounts;
    menu.folders.clear();
    menu.accounts.clear();

    const auto& folders_layout = input.folders.layout();
    const auto folder_rows = input.folders.rows();
    const auto& accounts_layout = input.accounts.layout();
    const auto account_rows = input.accounts.rows();

    const auto root = input.folders.root(domain::folder_tree::accounts,
        instance_id);
    if (root) {
        menu.folders.push_back(root->index);
        const auto walk = [&](auto&& self, std::string_view parent) -> void {
            for (std::uint32_t index = 0; index < folders_layout.capacity();
                 ++index) {
                if (!folders_layout.is_alive(index))
                    continue;
                const auto& folder = folder_rows[index];
                if (folder.tree != domain::folder_tree::accounts
                    || folder.owner_id != instance_id
                    || folder.parent_id != parent)
                    continue;
                menu.folders.push_back(index);
                self(self, folder.id);
            }
        };
        walk(walk, input.folders.get(*root)->id);
    }

    bool acted = false;
    for (const std::uint32_t slot : menu.folders) {
        const auto& folder = folder_rows[slot];
        const bool is_root = root && slot == root->index;
        const std::size_t first = menu.accounts.size();
        for (std::uint32_t index = 0; index < accounts_layout.capacity();
             ++index) {
            if (accounts_layout.is_alive(index)
                && account_rows[index].instance_id == instance_id
                && account_rows[index].folder_id == folder.id)
                menu.accounts.push_back(index);
        }
        std::sort(menu.accounts.begin()
                + static_cast<std::ptrdiff_t>(first),
            menu.accounts.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
                return folded_less(account_label(account_rows[lhs]),
                    account_label(account_rows[rhs]));
            });
        if (first == menu.accounts.size())
            continue;
        if (!is_root)
            widget::menu_heading(folder.name.c_str());
        for (std::size_t i = first; i < menu.accounts.size(); ++i) {
            const auto& row = account_rows[menu.accounts[i]];
            if (draw_account_row(input, state, row, environment)) {
                acted = true;
                break;
            }
        }
        if (acted)
            return true;
    }

    /*
     * An account whose folder no longer exists would otherwise vanish from the
     * menu. It is still a login, so it is listed last under the words that say
     * why it is not under a heading.
     */
    const std::size_t first = menu.accounts.size();
    for (std::uint32_t index = 0; index < accounts_layout.capacity(); ++index) {
        if (!accounts_layout.is_alive(index))
            continue;
        const auto& row = account_rows[index];
        if (row.instance_id != instance_id)
            continue;
        const bool known = std::any_of(menu.folders.begin(),
            menu.folders.end(), [&](std::uint32_t slot) {
                return folder_rows[slot].id == row.folder_id;
            });
        if (!known)
            menu.accounts.push_back(index);
    }
    std::sort(menu.accounts.begin() + static_cast<std::ptrdiff_t>(first),
        menu.accounts.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
            return folded_less(account_label(account_rows[lhs]),
                account_label(account_rows[rhs]));
        });
    if (first != menu.accounts.size()) {
        widget::menu_heading("No folder");
        for (std::size_t i = first; i < menu.accounts.size(); ++i) {
            if (draw_account_row(input, state,
                    account_rows[menu.accounts[i]], environment))
                return true;
        }
    }
    return false;
}

/**
 * @brief ACCOUNTS: one submenu per environment, then its accounts by folder.
 *
 * The environment rows are the instances that hold a login. Hovering or
 * clicking one opens its accounts beside it; a folder is a heading rather than
 * a row, and every account under it signs in with one click.
 */
void draw_accounts_menu(const render_input& input,
    widget::menu_bar_state& state) {
    auto& menu = state.accounts;
    collect_environments(input, menu);
    if (menu.environments.empty()) {
        widget::menu_item_row({.label = "No accounts yet", .enabled = false});
        return;
    }

    const auto rows = input.instances.rows();
    for (const std::uint32_t slot : menu.environments) {
        const auto& environment = rows[slot];
        char count[24];
        std::snprintf(count, sizeof count, "%zu",
            account_count(input, environment.id));
        ImGui::PushID(environment.id.c_str());
        const bool pressed = widget::menu_item_row({.label = environment.name.c_str(),
            .shortcut = count, .submenu = true});
        const ImVec2 anchor(ImGui::GetItemRectMax().x,
            ImGui::GetItemRectMin().y);
        if ((pressed || ImGui::IsItemHovered())
            && !ImGui::IsPopupOpen(accounts_of_id))
            ImGui::OpenPopup(accounts_of_id);
        ImGui::SetNextWindowPos(anchor);
        bool acted = false;
        {
            const widget::menu_popup panel(accounts_of_id);
            if (panel)
                acted = draw_environment_accounts(input, state,
                    environment.id, environment.name);
        }
        ImGui::PopID();
        if (acted) {
            /*
             * A pick closes the submenu and the ACCOUNTS menu with it, so the
             * sign-in, or its password form, is what the operator is left with.
             */
            ImGui::ClosePopupToLevel(0, true);
            return;
        }
    }
}

/** @brief The password form for an account the store holds no password for. */
void draw_sign_in_form(const render_input& input,
    widget::menu_bar_state& state) {
    auto& form = state.sign_in;
    if (!form.open)
        return;
    ImGui::OpenPopup(sign_in_popup_id);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal(sign_in_popup_id, nullptr, flags))
        return;

    ImGui::Text("Sign in to %s as %s", form.instance_name.c_str(),
        form.principal.c_str());
    const bool store_open = input.store_lock == store_lock::unlocked;
    if (!store_open) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("The store is locked, so this password cannot be "
                           "remembered.");
        ImGui::PopStyleColor();
    }

    ImGui::SetNextItemWidth(360.0f);
    if (form.focus) {
        ImGui::SetKeyboardFocusHere();
        form.focus = false;
    }
    ImGui::InputTextWithHint("##menu-sign-in-password", "password",
        form.password.data(), form.password.size(),
        ImGuiInputTextFlags_Password);

    if (!store_open)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Remember this password", &form.remember);
    if (!store_open)
        ImGui::EndDisabled();

    const bool complete = form.password[0] != '\0';
    if (!complete)
        ImGui::BeginDisabled();
    const bool submit = ImGui::Button("Sign in");
    if (!complete)
        ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel");

    if (submit && complete) {
        input.commands.push(account_sign_in_command(form.instance_id,
            form.account_id, form.principal, form.password.data(),
            form.remember && store_open));
        raise_shortcut_activated(input, std::format("Sign in to {} as {}",
            form.instance_name, form.principal));
    }
    if ((submit && complete) || cancel) {
        std::fill(form.password.begin(), form.password.end(), '\0');
        form.remember = false;
        form.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

}

/** @brief TOOLS: the windows that belong to a signed-in session. */
void draw_tools_menu(const render_input& input) {
    namespace domain = hedgr::model::domain;
    const auto session = current_session(input);
    if (widget::menu_item_row({.label = "Series browser",
            .enabled = session.has_value()}))
        request_session_window(input, *session,
            domain::window_kind::series_browser);
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
                [&] { draw_view_menu(input, desk_docked, result); }, height);
            ImGui::SameLine();
            widget::menu_bar_label(state, instances_entry, "Instances",
                [&] { draw_instances_panel(input, state.instances, result); },
                height,
                ImVec2(theme::instances_panel_width,
                    theme::instances_panel_height));
            if (state.open != instances_entry)
                state.instances.shown = false;
            ImGui::SameLine();
            widget::menu_bar_label(state, accounts_entry, "Accounts",
                [&] { draw_accounts_menu(input, state); }, height);
            ImGui::SameLine();
            widget::menu_bar_label(state, tools_entry, "Tools",
                [&] { draw_tools_menu(input); }, height);
        }
        /*
         * HELP is always drawn: About is what a person asks for when the store
         * will not open, and hiding it with the session menus leaves them with
         * nothing to read.
         */
        ImGui::SameLine();
        widget::menu_bar_label(state, help_entry, "Help", [&] {
            if (widget::menu_item_row({.label = "About"}))
                state.about_open = true;
        }, height);

        /* The hairline round the plate, drawn over the fill. */
        draw->AddRect(ImVec2(plate_min.x + 0.5f, plate_min.y + 0.5f),
            ImVec2(plate_max.x - 0.5f, plate_max.y - 0.5f),
            u32(theme::menu_bar_edge()), theme::menu_bar_rounding);

        /*
         * The password form is opened from this window, so it is drawn here
         * rather than beside the About window: a popup belongs to the window
         * that opened it, and the menu bar is what the ACCOUNTS rows are. It
         * comes after the item spacing is popped, so the form lays out with
         * the ordinary spacing.
         */
        ImGui::PopStyleVar();
        draw_sign_in_form(input, state);
    }
    ImGui::End();
    draw_about(input, logo_svg, state);
}

}
