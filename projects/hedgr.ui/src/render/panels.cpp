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
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include "imgui.h"
#include "ores.logging/make_logger.hpp"
#include <algorithm>

#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace {

namespace domain = hedgr::model::domain;
using hedgr::ui::render::render_input;

/** @brief This file's logger, which says why a header carries no flags. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.panels");
    return instance;
}

/** @brief An instance's name by key, falling back to the key itself. */
[[nodiscard]] std::string_view instance_name(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.instances.find(id))
        return input.instances.get(*handle)->name;
    return id;
}

/** @brief An account's principal by key, which is what a workspace titles. */
[[nodiscard]] std::string_view account_principal(const render_input& input,
    std::string_view id) {
    if (const auto handle = input.accounts.find(id))
        return input.accounts.get(*handle)->principal;
    return id;
}


/** @brief @p text, or @p fallback when it is empty. */
[[nodiscard]] std::string_view or_else(std::string_view text,
    std::string_view fallback) {
    return text.empty() ? fallback : text;
}

template<class... Args>
void put_format(std::string& out, std::format_string<Args...> format,
    Args&&... args) {
    std::format_to(std::back_inserter(out), format,
        std::forward<Args>(args)...);
}

}

namespace hedgr::ui::render {

void append_window_title(std::string& out, domain::window_kind kind,
    const domain::window_state& state, const render_input& input) {
    const auto put = [&out](std::string_view text) { out += text; };
    /*
     * Read one alternative at a time rather than switching over the kind and
     * then reading the state: the kind and the state's alternative are set from
     * the same command and cannot disagree for a window that came through the
     * store, but a window built by hand in a test can, and a title function
     * should report what the state says rather than crash on the mismatch.
     */
    if (const auto* chart = std::get_if<domain::chart_state>(&state)) {
        if (kind == domain::window_kind::chart)
            return put(or_else(chart->label, "Chart"));
    }
    if (const auto* card = std::get_if<domain::mini_plot_state>(&state)) {
        if (kind == domain::window_kind::mini_plot)
            return put(or_else(card->label, "Mini plot"));
    }
    if (const auto* grid = std::get_if<domain::grid_state>(&state)) {
        if (kind == domain::window_kind::grid)
            return put(or_else(grid->resource, "Grid"));
    }
    if (const auto* notice = std::get_if<domain::notice_state>(&state)) {
        if (kind == domain::window_kind::notice)
            return put(or_else(notice->title, "Notice"));
    }
    if (const auto* accounts = std::get_if<domain::accounts_state>(&state)) {
        if (kind == domain::window_kind::accounts) {
            return put_format(out, "Accounts: {}",
                instance_name(input, accounts->instance_id));
        }
    }
    if (const auto* centre = std::get_if<domain::command_centre_state>(&state)) {
        if (kind == domain::window_kind::command_centre) {
            return put_format(out, "Command centre: {}",
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
            return put(account_principal(input, session->account_id));
        }
    }

    switch (kind) {
    case domain::window_kind::chart:
        return put("Chart");
    case domain::window_kind::mini_plot:
        return put("Mini plot");
    case domain::window_kind::grid:
        return put("Grid");
    case domain::window_kind::notice:
        return put("Notice");
    case domain::window_kind::instance_manager:
        return put("Instances");
    case domain::window_kind::accounts:
        return put("Accounts");
    case domain::window_kind::workspace:
        return put("Workspace");
    case domain::window_kind::command_centre:
        return put("Command centre");
    case domain::window_kind::series_browser:
        return put("Market data");
    case domain::window_kind::crm_matrix: {
        if (const auto* crm = std::get_if<domain::crm_matrix_state>(&state))
            if (!crm->crm_name.empty())
                return put_format(out, "Cross rates \u00b7 {}", crm->crm_name);
        return put("Cross rates");
    }
    case domain::window_kind::term_structure: {
        if (const auto* terms =
                std::get_if<domain::term_structure_state>(&state)) {
            return put(or_else(terms->label, "Term structure"));
        }
        return put("Term structure");
    }
    case domain::window_kind::vol_surface: {
        if (const auto* surface =
                std::get_if<domain::vol_surface_state>(&state)) {
            return put(or_else(surface->label, "Volatility surface"));
        }
        return put("Volatility surface");
    }
    }
    return put("Window");
}

bool shows_instances(const render_input& input) noexcept {
    return input.store_lock == domain::store_lock::unlocked;
}

std::optional<hedgr::model::state::session_handle> signed_in_session(
    const render_input& input, hedgr::model::state::window_handle handle) {
    const auto session = input.windows.session_of(handle);
    if (session.has_value()
        && hedgr::ui::widget::view_state_of(session_view_facts(input, handle))
                .state == hedgr::ui::widget::view_state::none)
        return session;
    return std::nullopt;
}

hedgr::ui::widget::view_facts session_view_facts(const render_input& input,
    hedgr::model::state::window_handle handle) {
    hedgr::ui::widget::view_facts facts;
    const auto session = input.windows.session_of(handle);
    if (!session)
        return facts;
    if (!input.sessions.contains(*session)) {
        facts.ended = true;
        return facts;
    }
    facts.session = input.sessions.status(*session);
    facts.has_token = !input.sessions.token(*session).empty();
    facts.session_error = input.sessions.last_error(*session);
    return facts;
}

void draw_session_state(const render_input& input,
    hedgr::model::state::window_handle handle,
    hedgr::ui::widget::status_bar& status,
    hedgr::ui::widget::foot_state& foot) {
    hedgr::ui::widget::draw_view_state_body(status, foot,
        hedgr::ui::widget::view_state_of(session_view_facts(input, handle)));
    hedgr::ui::widget::draw_status_foot(status, input.now_ns, foot);
}

identity_bar_state identity_of(const render_input& input,
    hedgr::model::state::window_handle handle) {
    identity_bar_state result;

    if (const auto session = input.windows.session_of(handle);
        session && input.sessions.contains(*session)) {
        result.signed_in = signed_in_session(input, handle).has_value();
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

void request_flag(const render_input& input,
    hedgr::model::state::session_handle session, std::string_view currency) {
    const auto tenant_id = input.sessions.tenant_id(session);
    if (tenant_id.empty() || currency.empty())
        return;
    const auto* images = input.currency_images.get(tenant_id);
    if (images == nullptr
        || images->state != hedgr::model::state::image_map_state::ready)
        return;

    const auto* image_id = input.currency_images.image_for(tenant_id, currency);
    if (image_id == nullptr || input.images.contains(tenant_id, *image_id))
        return;
    const auto key = hedgr::model::state::make_tenant_key(tenant_id, *image_id);
    if (input.failed_images.contains(key)
        || input.pending_images.contains(key))
        return;
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::load_image;
    request.instance_id = std::string{input.sessions.instance_id(session)};
    request.account_id = std::string{input.sessions.account_id(session)};
    request.image_id = *image_id;
    input.commands.push(std::move(request));
}

namespace {

/** @brief The catalogue row a session lists @p series_id as, or nullptr. */
[[nodiscard]] const domain::market_series_row* catalogue_row(
    const render_input& input,
    hedgr::model::state::session_handle session, std::string_view series_id) {
    if (series_id.empty())
        return nullptr;
    const auto* catalogue = input.market.catalogue(
        input.sessions.instance_id(session),
        input.sessions.account_id(session));
    return catalogue != nullptr ? catalogue->row_of(series_id) : nullptr;
}

/**
 * @brief Whether the keys in @p parts have all been seen before.
 *
 * A drawing path runs sixty times a second over every visible row, so a
 * diagnostic that repeats itself has to be suppressed without allocating:
 * building the message to look it up would cost a heap block per row per frame,
 * which is the very frame cost these screens are measured for. The keys are
 * hashed end to end instead, with a separator that no id contains.
 */
[[nodiscard]] bool seen(std::initializer_list<std::string_view> parts) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto part : parts) {
        for (const char c : part) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        hash ^= 0x1full;
        hash *= 1099511628211ull;
    }
    static std::unordered_set<std::uint64_t> said;
    if (!said.insert(hash).second)
        return true;
    if (said.size() > 512)
        said.clear();
    return false;
}

/** @brief Said once per series, on the first frame a name had to be parsed. */
void trace_label_fallback(std::string_view series_id, std::string_view label,
    std::string_view why) {
    if (seen({series_id, label, why}))
        return;
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Flags for '" << label << "' (" << series_id << ") read from the "
        << "label, " << why;
}

}

hedgr::ui::widget::currency_pair currencies_of_series(
    const render_input& input,
    hedgr::model::state::session_handle session, std::string_view series_id,
    std::string_view label) {
    if (const auto* row = catalogue_row(input, session, series_id)) {
        if (!row->identity.empty()) {
            auto pair = hedgr::ui::widget::currencies_of(row->identity);
            if (!pair.base.empty())
                return pair;
            trace_label_fallback(series_id, label,
                "because its subject is not a currency");
        } else {
            trace_label_fallback(series_id, label,
                "because its identity was not parsed");
        }
    } else {
        trace_label_fallback(series_id, label,
            "because the catalogue does not list it");
    }
    return hedgr::ui::widget::currencies_of(label);
}

void request_flags(const render_input& input,
    hedgr::model::state::session_handle session,
    const hedgr::ui::widget::currency_pair& pair) {
    request_flag(input, session, pair.base);
    request_flag(input, session, pair.quote);
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

void trace_absent_flags(const render_input& input,
    hedgr::model::state::session_handle session,
    const hedgr::ui::widget::currency_pair& pair) {
    /*
     * A header with no flags says nothing about why: the tenant, the map, the
     * currency and the image bytes are four different answers, and the one that
     * is missing is the one an operator has to be told about.
     */
    const auto tenant_id = input.sessions.tenant_id(session);
    const auto* map = input.currency_images.get(tenant_id);
    const auto* image_id =
        input.currency_images.image_for(tenant_id, pair.base);
    const auto* image = image_id != nullptr
        ? input.images.get(tenant_id, *image_id) : nullptr;

    /*
     * Said once for each state, not once a frame. The reason a header has no
     * flag changes when the map arrives or an image lands, and those are the
     * moments worth a line; a header that repeats the same sentence sixty times
     * a second buries them.
     */
    const auto map_state = map == nullptr ? std::string_view{"absent"}
        : map->state == hedgr::model::state::image_map_state::ready
        ? std::string_view{"ready"} : std::string_view{"not ready"};
    const auto image_state =
        image_id == nullptr ? std::string_view{"none"} : "named";
    const auto bytes_state =
        image == nullptr ? std::string_view{"absent"} : "held";
    const auto texture_state =
        input.textures == nullptr ? std::string_view{"none"} : "present";
    if (seen({pair.base, pair.quote, tenant_id, map_state, image_state,
            bytes_state, texture_state}))
        return;
    /*
     * Info, not debug. A header with no flag is an operator-visible fault, and
     * the four facts it is made of - tenant, map, image id, bytes - are the only
     * way to tell a missing map from a refused image. The line is emitted once
     * per state, so a steady window costs nothing.
     */
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "No flags for currencies '" << pair.base
        << "' / '" << pair.quote << "', tenant "
        << (tenant_id.empty() ? "none" : std::string{tenant_id})
        << ", map " << map_state
        << ", image id " << image_state
        << ", bytes " << bytes_state
        << ", textures " << texture_state;
}

std::string identity_text(const identity_bar_state& state) {
    if (!state.signed_in)
        return "Not signed in";

    std::string text = state.principal.empty()
        ? "no principal" : state.principal;
    text += " \u00b7 ";
    text += state.tenant.empty() ? "no tenant" : state.tenant;
    text += " \u00b7 ";
    text += state.party.empty() ? "no party" : state.party;
    return text;
}

float series_flags_width(const render_input& input,
    const hedgr::model::state::session_handle session,
    const hedgr::ui::widget::currency_pair& pair) {
    request_flags(input, session, pair);
    return hedgr::ui::widget::flag_pair_width(
        hedgr::ui::widget::value_pill_height()
            * hedgr::ui::theme::flag_height_fraction,
        image_texture(input, session, pair.base),
        image_texture(input, session, pair.quote));
}

void trace_view_state(std::string_view screen, std::string_view subject,
    hedgr::ui::render::window_ui_state& state,
    const hedgr::ui::widget::view_verdict& verdict) {
    using hedgr::ui::widget::view_state;

    const auto word = [](view_state value) -> const char* {
        switch (value) {
        case view_state::none:
            return "drawing";
        case view_state::reading:
            return "reading";
        case view_state::not_connected:
            return "no session";
        case view_state::error:
            return "error";
        case view_state::no_data:
            return "no data";
        }
        return "?";
    };

    const auto code = static_cast<int>(verdict.state);
    if (state.traced.state == code
        && state.traced.reason == verdict.reason)
        return;
    state.traced.state = code;
    state.traced.reason = std::string{verdict.reason};

    if (verdict.reason.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Window '" << (subject.empty() ? std::string_view{"?"} : subject)
            << "' (" << screen << "): " << word(verdict.state);
        return;
    }
    BOOST_LOG_SEV(lg(), verdict.state == view_state::error
            ? ores::logging::warn : ores::logging::info)
        << "Window '" << (subject.empty() ? std::string_view{"?"} : subject)
        << "' (" << screen << "): " << word(verdict.state) << " - "
        << verdict.reason;
}

void draw_series_flags(const render_input& input,
    const hedgr::model::state::session_handle session,
    const hedgr::ui::widget::currency_pair& pair, float inset) {
    request_flags(input, session, pair);

    const auto base_texture = image_texture(input, session, pair.base);
    const auto quote_texture = image_texture(input, session, pair.quote);
    const float pill_h = hedgr::ui::widget::value_pill_height();
    const float flag_h = pill_h * hedgr::ui::theme::flag_height_fraction;
    const float flags_width =
        hedgr::ui::widget::flag_pair_width(flag_h, base_texture, quote_texture);
    if (flags_width == 0.0f) {
        /*
         * Nothing to draw, and the line still ends here. The caller places what
         * follows on the line below and reads its position from the cursor, so
         * returning with the cursor where the header left it hands a chart the
         * tail of its own title row: a plot one flag wide, pressed against the
         * right edge of the window.
         */
        trace_absent_flags(input, session, pair);
        ImGui::NewLine();
        return;
    }

    const float right = ImGui::GetWindowContentRegionMax().x - inset;
    if (ImGui::GetCursorPosX() < right - flags_width)
        ImGui::SetCursorPosX(right - flags_width);

    auto origin = ImGui::GetCursorScreenPos();
    origin.y += (pill_h - flag_h) * 0.5f;
    hedgr::ui::widget::draw_flag_pair(ImGui::GetWindowDrawList(), origin,
        flag_h, base_texture, quote_texture);
    ImGui::Dummy(ImVec2(flags_width, pill_h));
}

bool sign_in_needs_password(domain::store_lock lock,
    bool has_credential) noexcept {
    return lock != domain::store_lock::unlocked || !has_credential;
}

bool needs_party_choice(const render_input& input,
    hedgr::model::state::session_handle session) {
    if (input.sessions.status(session)
        != hedgr::model::domain::session_status::authenticated)
        return false;
    if (hedgr::model::event::party_count(input.sessions.parties(session)) < 2)
        return false;

    /*
     * The account is the row the choice is remembered on. A session whose
     * account the catalogue does not list yet is not asked: the store is where
     * the answer will be written, and asking before it is readable would put a
     * prompt on screen that could not save what it was told.
     */
    const auto handle = input.accounts.find(input.sessions.account_id(session));
    if (!handle)
        return false;
    const auto* row = input.accounts.get(*handle);
    return row != nullptr && row->party_id.empty();
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
    case domain::window_kind::crm_matrix:
    case domain::window_kind::mini_plot:
    case domain::window_kind::vol_surface:
        return true;
    case domain::window_kind::accounts:
    case domain::window_kind::workspace:
    case domain::window_kind::command_centre:
    case domain::window_kind::series_browser:
    case domain::window_kind::term_structure:
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

std::optional<hedgr::model::state::session_handle>
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

void request_instances_window(const render_input& input) {
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::open_window;
    request.new_window_kind = domain::window_kind::instance_manager;
    input.commands.push(std::move(request));
}

void raise_shortcut_activated(const render_input& input, std::string detail) {
    hedgr::model::signal::signal value;
    value.name = std::string(hedgr::model::signal::names::shortcut_activated);
    value.detail = std::move(detail);
    value.at_ns = input.now_ns;
    hedgr::model::signal::bus().raise(std::move(value));
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

bool shortcuts_list_holds(
    const hedgr::model::state::shortcut_table& table,
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
    if (shortcuts_list_holds(input.shortcuts, kind, target_id)) {
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
