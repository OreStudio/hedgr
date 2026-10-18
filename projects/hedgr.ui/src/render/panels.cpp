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
#include "hedgr.ui/render/panels.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include "imgui.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

namespace domain = hedgr::model::domain;
using hedgr::ui::render::render_input;

/** @brief An instance's name by key, falling back to the key itself. */
[[nodiscard]] std::string instance_name(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.instances.find(id))
        return input.instances.get(*handle)->name;
    return std::string{id};
}

/** @brief An account's name by key, falling back to the key itself. */
[[nodiscard]] std::string account_name(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.accounts.find(id)) {
        const auto* row = input.accounts.get(*handle);
        return row->name.empty() ? row->principal : row->name;
    }
    return std::string{id};
}

/** @brief An account's principal by key, which is what a workspace titles. */
[[nodiscard]] std::string account_principal(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.accounts.find(id))
        return input.accounts.get(*handle)->principal;
    return std::string{id};
}

}

namespace hedgr::ui::render {

std::string window_title(domain::window_kind kind,
    const domain::window_state& state, const render_input& input) {
    /*
     * Read one alternative at a time rather than switching over the kind and
     * then reading the state: the kind and the state's alternative are set from
     * the same command and cannot disagree for a window that came through the
     * store, but a window built by hand in a test can, and a title function
     * should report what the state says rather than crash on the mismatch.
     */
    if (const auto* chart = std::get_if<domain::chart_state>(&state)) {
        if (kind == domain::window_kind::chart)
            return chart->label.empty() ? std::string{"Chart"} : chart->label;
    }
    if (const auto* grid = std::get_if<domain::grid_state>(&state)) {
        if (kind == domain::window_kind::grid)
            return grid->resource.empty() ? std::string{"Grid"} : grid->resource;
    }
    if (const auto* notice = std::get_if<domain::notice_state>(&state)) {
        if (kind == domain::window_kind::notice)
            return notice->title.empty() ? std::string{"Notice"} : notice->title;
    }
    if (const auto* accounts = std::get_if<domain::accounts_state>(&state)) {
        if (kind == domain::window_kind::accounts) {
            return std::format("Accounts: {}",
                instance_name(input, accounts->instance_id));
        }
    }
    if (const auto* centre = std::get_if<domain::command_centre_state>(&state)) {
        if (kind == domain::window_kind::command_centre) {
            return std::format("Command centre: {}",
                instance_name(input, centre->instance_id));
        }
    }
    if (const auto* session = std::get_if<domain::workspace_state>(&state)) {
        if (kind == domain::window_kind::workspace) {
            /*
             * The title names the principal, because a wall may hold several
             * workspaces over the same instance and who is signed in is the
             * only thing that tells them apart. The instance follows it from
             * the session the window is bound to, which the frame loop knows
             * and this function does not: see the renderer, which appends it.
             */
            return account_principal(input, session->account_id);
        }
    }

    switch (kind) {
    case domain::window_kind::chart:
        return "Chart";
    case domain::window_kind::grid:
        return "Grid";
    case domain::window_kind::notice:
        return "Notice";
    case domain::window_kind::instance_manager:
        return "Instances";
    case domain::window_kind::accounts:
        return "Accounts";
    case domain::window_kind::workspace:
        return "Workspace";
    case domain::window_kind::command_centre:
        return "Command centre";
    case domain::window_kind::series_browser:
        return "Market data";
    }
    return "Window";
}

bool shows_instances(const render_input& input) noexcept {
    return input.store_lock == domain::store_lock::unlocked;
}

identity_bar_state identity_of(const render_input& input,
    hedgr::model::state::window_handle handle) {
    identity_bar_state result;

    if (const auto session = input.windows.session_of(handle)) {
        result.signed_in = input.sessions.status(*session)
                == domain::session_status::authenticated
            && !input.sessions.token(*session).empty();
        result.tenant = std::string{input.sessions.tenant_name(*session)};
        result.party = std::string{input.sessions.party_name(*session)};
        result.instance =
            instance_name(input, input.sessions.instance_id(*session));
        if (const auto account =
                input.accounts.find(input.sessions.account_id(*session))) {
            result.principal = input.accounts.get(*account)->principal;
        }
    } else {
        /*
         * A window that is not bound to a session still names the deployment
         * it belongs to: the accounts window and the command centre both carry
         * it in their state precisely so a bar can say "where" without a live
         * session to ask.
         */
        const auto& state = input.windows.state(handle);
        if (const auto* accounts =
                std::get_if<domain::accounts_state>(&state)) {
            result.instance = instance_name(input, accounts->instance_id);
        } else if (const auto* centre =
                       std::get_if<domain::command_centre_state>(&state)) {
            result.instance = instance_name(input, centre->instance_id);
        }
    }

    return result;
}

void request_flags(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view label) {
    const auto tenant_id = input.sessions.tenant_id(session);
    if (tenant_id.empty())
        return;
    const auto* images = input.currency_images.get(tenant_id);
    if (images == nullptr
        || images->state != hedgr::model::state::image_map_state::ready)
        return;

    const auto instance_id = input.sessions.instance_id(session);
    const auto account_id = input.sessions.account_id(session);
    const auto pair = hedgr::ui::widget::currencies_of(label);
    for (const auto& code : {pair.base, pair.quote}) {
        if (code.empty())
            continue;
        const auto* image_id =
            input.currency_images.image_for(tenant_id, code);
        if (image_id == nullptr || input.images.contains(tenant_id, *image_id))
            continue;
        const auto key =
            hedgr::model::state::make_tenant_key(tenant_id, *image_id);
        if (input.failed_images.contains(key)
            || input.pending_images.contains(key))
            continue;
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::load_image;
        request.instance_id = std::string{instance_id};
        request.account_id = std::string{account_id};
        request.image_id = *image_id;
        input.commands.push(std::move(request));
    }
}

ImTextureID image_texture(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view currency) {
    if (input.textures == nullptr)
        return 0;
    const auto tenant_id = input.sessions.tenant_id(session);
    if (tenant_id.empty())
        return 0;
    const auto* image_id = input.currency_images.image_for(tenant_id, currency);
    if (image_id == nullptr)
        return 0;
    const auto* image = input.images.get(tenant_id, *image_id);
    if (image == nullptr)
        return 0;
    return input.textures->get(tenant_id, *image_id, image->mime_type,
        image->data);
}

std::string identity_text(const identity_bar_state& state) {
    if (!state.signed_in)
        return "Not signed in";

    std::string text = state.principal;
    if (!state.tenant.empty())
        text += " @ " + state.tenant;
    text += state.party.empty() ? " [no party]" : " [" + state.party + "]";
    return text;
}

bool sign_in_needs_password(domain::store_lock lock,
    bool has_credential) noexcept {
    return lock != domain::store_lock::unlocked || !has_credential;
}

bool shows_window(domain::window_kind kind, const render_input& input,
    std::string_view window_id) noexcept {
    static_cast<void>(window_id);

    if (shows_instances(input))
        return true;

    /*
     * A closed lock does not hide everything: a notice, a chart and a grid are
     * surfaces whose contents arrive over a session rather than out of the
     * store, and a session is not encrypted by the master password. What the
     * lock closes is the catalogue -- the accounts windows and the workspaces
     * -- because every one of those reads rows that only the master password
     * opens.
     *
     * The Instances window stays. It is the shell the lock control lives on,
     * and it is the surface an operator is looking at when they decide to
     * open the store; taking it away would leave them with a program that had
     * apparently stopped being the one they were using. Its rows are blanked
     * instead, by the renderer that knows which part of it is catalogue.
     */
    switch (kind) {
    case domain::window_kind::chart:
    case domain::window_kind::grid:
    case domain::window_kind::notice:
    case domain::window_kind::instance_manager:
        return true;
    case domain::window_kind::accounts:
    case domain::window_kind::workspace:
    case domain::window_kind::command_centre:
    case domain::window_kind::series_browser:
        return false;
    }
    return false;
}

void request_accounts_window(const render_input& input,
    std::string_view instance_id) {
    if (!input.instances.find(instance_id))
        return;

    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::open_window;
    request.instance_id = std::string{instance_id};
    request.new_window_kind = domain::window_kind::accounts;
    input.commands.push(std::move(request));
}

void request_instances_window(const render_input& input) {
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::open_window;
    request.new_window_kind = domain::window_kind::instance_manager;
    input.commands.push(std::move(request));
}

std::uint64_t instance_icon_texture(const render_input& input,
    std::string_view instance_id) {
    if (input.instance_icons == nullptr)
        return 0;

    const auto handle = input.instances.find(instance_id);
    if (!handle)
        return 0;

    /*
     * An empty key is asked of the cache like any other, and the cache answers
     * zero for it because no asset is stored under an empty name. The branch is
     * not written out here because the answer is the same either way, and a
     * second rule for the empty case would be a second thing to keep in step.
     */
    const auto& row = *input.instances.get(*handle);
    return input.instance_icons->find(row.icon_key);
}

std::string shortcut_id_for(domain::window_kind kind,
    std::string_view target_id) {
    /*
     * The kind's canonical text is used rather than its number, so the key in
     * the store reads as what the shortcut opens and survives a window kind
     * being renumbered.
     */
    return std::format("sc:{}/{}", domain::to_string(kind), target_id);
}

bool shortcuts_bar_holds(const hedgr::model::state::shortcut_table& table,
    domain::window_kind kind, std::string_view target_id) {
    for (const auto& row : table.rows()) {
        if (row.target_kind == kind
            && std::string_view{row.target_id} == target_id)
            return true;
    }
    return false;
}

std::int64_t next_shortcut_position(
    const hedgr::model::state::shortcut_table& table) noexcept {
    /*
     * A scan rather than a cached maximum, because the table is what the store
     * wrote and a cached number would be one more thing a removal could leave
     * stale. The bar holds tens of entries, so the scan costs nothing.
     */
    std::int64_t highest = 0;
    for (const auto& row : table.rows()) {
        if (row.position >= highest)
            highest = row.position + 1;
    }
    return highest;
}

hedgr::model::command::command shortcut_toggle_command(
    const render_input& input, domain::window_kind kind,
    std::string_view target_id) {
    using hedgr::model::command::command_kind;

    hedgr::model::command::command request;
    request.shortcut_row.id = shortcut_id_for(kind, target_id);
    if (shortcuts_bar_holds(input.shortcuts, kind, target_id)) {
        request.kind = command_kind::remove_shortcut;
        return request;
    }

    request.kind = command_kind::save_shortcut;
    request.shortcut_row.target_kind = kind;
    request.shortcut_row.target_id = std::string{target_id};
    request.shortcut_row.position = next_shortcut_position(input.shortcuts);
    return request;
}

startup_notice startup_notice_for(std::string_view startup_failure) {
    /*
     * Empty in, empty out. There is nothing to warn about, and the frame reads
     * @c empty as the instruction not to draw the notice at all, so a healthy
     * store draws exactly what it drew before this existed.
     */
    if (startup_failure.empty())
        return startup_notice{};

    /*
     * The heading names the fault and the reason below it is the runtime's own
     * sentence, unchanged. The store whose schema version is refused is the one
     * that reaches an operator: its sentence names the file, both versions and
     * the remedy, and restating any of that here would be a second copy of the
     * store's diagnosis for the two to drift apart.
     */
    return startup_notice{
        .heading = "Hedgr could not open its store.",
        .reason = std::string{startup_failure},
    };
}

}
