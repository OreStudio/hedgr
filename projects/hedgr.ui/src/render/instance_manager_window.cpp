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
#include "internal.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::identity;
using hedgr::model::domain::instance;
using hedgr::model::domain::session_status;
using hedgr::model::domain::tier;
using hedgr::model::domain::window_kind;
using hedgr::model::state::session_handle;
using hedgr::model::state::window_handle;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/*
 * The three pieces of form state, one each. There is exactly one Instance
 * Manager window, so one of each is exactly enough; a second copy of this
 * surface would move them onto the workspace.
 */
instance add_draft{};
bool add_form_open = false;

/** @brief The instance whose identities are listed, empty when none is. */
std::string selected_instance_id;

/** @brief The identity whose connect form is showing, empty when none is. */
std::string connect_identity_id;

/**
 * @brief Whether a needle occurs anywhere in a haystack, case-insensitively.
 *
 * Filtering is the one place the interface wants Latin case folding; the rest
 * of the model keeps its stored text exactly as it found it.
 */
bool filter_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    return std::search(haystack.begin(), haystack.end(),
               needle.begin(), needle.end(),
               [&](char a, char b) { return lower(a) == lower(b); })
        != haystack.end();
}

/**
 * @brief An icon-only action button, sized to the frame height.
 */
bool action_button(const char* id, const char* glyph, const char* tooltip,
    const ImVec4* tint = nullptr) {
    const float height = ImGui::GetFrameHeight();
    if (tint != nullptr)
        ImGui::PushStyleColor(ImGuiCol_Text, *tint);
    ImGui::PushID(id);
    const bool pressed = ImGui::Button(glyph, ImVec2(height, height));
    ImGui::PopID();
    if (tint != nullptr)
        ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

/**
 * @brief The colour of the destructive action, read from the tier.
 *
 * Removing a production instance and removing a development one are not the
 * same act, and the icon says so before the click rather than after it.
 */
ImVec4 destructive_tint(tier value) {
    return ImGui::ColorConvertU32ToFloat4(
        hedgr::ui::theme::tier_colour(value));
}

/**
 * @brief One session's row, drawn from the session table's hot columns.
 */
struct session_row final {
    /** @brief Handle of the session this row draws. */
    session_handle handle;

    /** @brief Status, read straight from the status column. */
    session_status status = session_status::disconnected;

    /** @brief Round-trip latency in microseconds, from the latency column. */
    std::int64_t latency_us = 0;

    /** @brief Stable id of the instance the session belongs to. */
    std::string instance_id;

    /** @brief Stable id of the identity the session presents. */
    std::string identity_id;
};

/**
 * @brief Every live session, gathered from the hot columns and the warm
 * accessors.
 *
 * Gathered once per frame because the table is read for two different
 * purposes: the sessions list, and the per-session row of window-opening
 * icons. Walking the slab twice would read the same columns twice.
 */
std::vector<session_row> collect_sessions(const render_input& input) {
    std::vector<session_row> rows;
    const auto& sessions = input.sessions;
    const auto& layout = sessions.layout();
    const auto status_column = sessions.status_column();
    const auto latency_column = sessions.latency_us_column();

    for (std::size_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        const session_handle handle{static_cast<std::uint32_t>(index),
            layout.generation[index]};
        session_row row;
        row.handle = handle;
        row.status = static_cast<session_status>(status_column[handle.index]);
        row.latency_us = latency_column[handle.index];
        row.instance_id = std::string{sessions.instance_id(handle)};
        row.identity_id = std::string{sessions.identity_id(handle)};
        rows.push_back(std::move(row));
    }
    return rows;
}

/** @brief The label to show for an identity, falling back to its principal. */
std::string identity_label(const identity& row) {
    if (!row.label.empty())
        return row.label;
    return row.principal;
}

/**
 * @brief The name to show for an instance id, falling back to the id itself.
 *
 * A session may outlive the instance it points at, and a row that renders an
 * empty cell tells the reader nothing about why.
 */
std::string instance_name(const render_input& input, std::string_view id) {
    if (const auto* row = input.instances.find(id))
        return row->name;
    return std::string{id};
}

/**
 * @brief The form that creates an instance, shown on request.
 *
 * Tier is chosen explicitly here and never inferred from the name, matching
 * how the rest of the interface reads it.
 */
void draw_add_form(const render_input& input) {
    constexpr std::array<std::pair<tier, std::string_view>, 3> tiers{{
        {tier::development, "Development"},
        {tier::staging, "Staging"},
        {tier::production, "Production"},
    }};

    ImGui::SeparatorText("New instance");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-name", "name", &add_draft.name);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-url", "nats://host:port",
        &add_draft.nats_url);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-prefix", "subject prefix",
        &add_draft.subject_prefix);

    ImGui::TextUnformatted("Tier");
    for (const auto& [value, label] : tiers) {
        ImGui::SameLine();
        const bool current = add_draft.tier == value;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::ColorConvertU32ToFloat4(
                    hedgr::ui::theme::tier_colour(value)));
        ImGui::PushID(label.data());
        if (ImGui::Button(hedgr::ui::theme::tier_icon(value)))
            add_draft.tier = value;
        ImGui::PopID();
        if (current)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", label.data());
    }

    const bool complete = !add_draft.name.empty()
        && !add_draft.nats_url.empty() && !add_draft.subject_prefix.empty();

    if (!complete)
        ImGui::BeginDisabled();
    const bool saved = ImGui::Button(hedgr::ui::icon::save);
    ImGui::SetItemTooltip("Save instance");
    if (!complete)
        ImGui::EndDisabled();

    if (saved && complete) {
        command request;
        request.kind = command_kind::save_instance;
        request.instance_id = add_draft.id;
        request.instance_row = add_draft;
        input.commands.push(std::move(request));
        add_draft = instance{};
        add_form_open = false;
    }

    ImGui::SameLine();
    const bool cancelled = ImGui::Button(hedgr::ui::icon::cancel);
    ImGui::SetItemTooltip("Cancel");
    if (cancelled) {
        add_draft = instance{};
        add_form_open = false;
    }
}

void draw_instances_table(const render_input& input, window_ui_state& state) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("##instances", 4, flags, ImVec2(0.0f, 160.0f)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##tier", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (const auto& row : input.instances.rows()) {
        if (!filter_contains(row.name, state.filter.data())
            && !filter_contains(row.subject_prefix, state.filter.data())
            && !filter_contains(row.nats_url, state.filter.data()))
            continue;

        ImGui::TableNextRow();
        ImGui::PushID(row.id.c_str());

        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(
                hedgr::ui::theme::tier_colour(row.tier)));
        ImGui::TextUnformatted(hedgr::ui::theme::tier_icon(row.tier));
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s",
            hedgr::model::domain::to_string(row.tier).data());

        ImGui::TableNextColumn();
        const bool selected = selected_instance_id == row.id;
        if (ImGui::Selectable(row.name.c_str(), selected))
            selected_instance_id = selected ? std::string{} : row.id;
        ImGui::SetItemTooltip("%s\n%s", row.name.c_str(), row.nats_url.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.subject_prefix.c_str());

        ImGui::TableNextColumn();
        if (action_button("edit", hedgr::ui::icon::edit, "Edit instance"))
            selected_instance_id = row.id;
        ImGui::SameLine();
        const ImVec4 danger = destructive_tint(row.tier);
        if (action_button("remove", hedgr::ui::icon::remove,
                "Remove instance", &danger)) {
            command request;
            request.kind = command_kind::remove_instance;
            request.instance_id = row.id;
            input.commands.push(std::move(request));
        }
        ImGui::SameLine();
        if (action_button("connect", hedgr::ui::icon::connect,
                "Connect an identity")) {
            selected_instance_id = row.id;
            state.selected_row = -1;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void draw_identities(const render_input& input, window_ui_state& state) {
    ImGui::SeparatorText("Identities");

    const auto children = input.identities.children_of(selected_instance_id);
    if (children.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("No identities on this instance yet.");
        ImGui::PopStyleColor();
        return;
    }

    const float reserved = ImGui::GetFrameHeight() * 2.0f
        + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const identity& row = *children[index];
        if (!filter_contains(identity_label(row), state.filter.data())
            && !filter_contains(row.principal, state.filter.data()))
            continue;

        ImGui::PushID(row.id.c_str());
        const bool selected = state.selected_row == static_cast<int>(index);
        if (ImGui::Selectable(identity_label(row).c_str(), selected,
                ImGuiSelectableFlags_None,
                ImVec2(ImGui::GetContentRegionAvail().x - reserved, 0.0f)))
            state.selected_row = selected ? -1 : static_cast<int>(index);
        ImGui::SetItemTooltip("%s", row.principal.c_str());

        ImGui::SameLine();
        if (action_button("connect", hedgr::ui::icon::connect,
                "Connect this identity")) {
            connect_identity_id = row.id;
            state.draft_principal = row.principal;
        }
        ImGui::SameLine();
        if (action_button("remove", hedgr::ui::icon::remove,
                "Remove identity")) {
            command request;
            request.kind = command_kind::remove_identity;
            request.instance_id = selected_instance_id;
            request.identity_id = row.id;
            input.commands.push(std::move(request));
        }
        ImGui::PopID();
    }
}

void draw_connect_form(const render_input& input, window_ui_state& state) {
    ImGui::SeparatorText("Connect");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##principal", "principal",
        &state.draft_principal);

    /*
     * The reveal toggle sits immediately right of the password box, because
     * that is where an eye is looked for. It is an icon button rather than the
     * hand-drawn glyph the old login dialog used, now that the interface has a
     * font that can carry one.
     */
    const float eye = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-(eye + ImGui::GetStyle().ItemInnerSpacing.x));
    ImGui::InputTextWithHint("##password", "password",
        state.draft_password.data(), state.draft_password.size(),
        state.password_visible ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (action_button("reveal",
            state.password_visible ? hedgr::ui::icon::eye_off
                                   : hedgr::ui::icon::eye,
            state.password_visible ? "Hide password" : "Show password"))
        state.password_visible = !state.password_visible;

    const bool complete = !state.draft_principal.empty()
        && state.draft_password[0] != '\0';

    if (!complete)
        ImGui::BeginDisabled();
    const bool confirmed = ImGui::Button(hedgr::ui::icon::save);
    ImGui::SetItemTooltip("Connect");
    if (!complete)
        ImGui::EndDisabled();

    if (confirmed && complete) {
        /*
         * The credential travels in the command and nowhere else. The
         * interface leaves its own scratch copy alone: the runtime owns
         * clearing it once the attempt resolves, and clearing it here would
         * race that.
         */
        command request;
        request.kind = command_kind::connect_session;
        request.instance_id = selected_instance_id;
        request.identity_id = connect_identity_id;
        request.principal = state.draft_principal;
        request.password = state.draft_password.data();
        input.commands.push(std::move(request));
    }

    ImGui::SameLine();
    const bool cancelled = ImGui::Button(hedgr::ui::icon::cancel);
    ImGui::SetItemTooltip("Cancel");
    if (cancelled) {
        state.draft_principal.clear();
        state.draft_password.fill('\0');
        connect_identity_id.clear();
    }
}

void draw_sessions_table(const render_input& input) {
    ImGui::SeparatorText("Sessions");

    const auto rows = collect_sessions(input);
    if (rows.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("No sessions. Connect an identity to start.");
        ImGui::PopStyleColor();
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("##sessions", 5, flags, ImVec2(0.0f, 160.0f)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Instance", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Identity", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Latency", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (const auto& row : rows) {
        const bool failed = row.status == session_status::failed;

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(row.handle.index));

        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(
                hedgr::ui::theme::status_colour(row.status)));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(hedgr::ui::theme::status_icon(row.status));
        ImGui::SetItemTooltip("%s",
            hedgr::model::domain::to_string(row.status).data());
        ImGui::PopStyleColor();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(instance_name(input, row.instance_id).c_str());

        ImGui::TableNextColumn();
        if (const auto* found = input.identities.find(row.identity_id))
            ImGui::TextUnformatted(identity_label(*found).c_str());
        else
            ImGui::TextUnformatted(row.identity_id.c_str());

        ImGui::TableNextColumn();
        if (row.status == session_status::authenticated) {
            ImGui::Text("%.1f ms",
                static_cast<double>(row.latency_us) / 1000.0);
        } else {
            ImGui::TextUnformatted("-");
        }

        ImGui::TableNextColumn();
        if (action_button("disconnect", hedgr::ui::icon::disconnect,
                "Disconnect")) {
            command request;
            request.kind = command_kind::disconnect_session;
            request.instance_id = row.instance_id;
            request.identity_id = row.identity_id;
            input.commands.push(std::move(request));
        }

        /*
         * The low-level cause belongs in a tooltip rather than in the cell: it
         * is a paragraph long and the cell is wide enough for a word. Pointing
         * at the row of a session that is red is exactly when it is wanted, so
         * it replaces the button's own tooltip.
         */
        if (failed) {
            const std::string error{input.sessions.last_error(row.handle)};
            ImGui::SetItemTooltip("Failed: %s", error.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

/**
 * @brief The row of icons that opens a new window on a session.
 *
 * One icon per kind, tooltipped, because a window is opened by naming what it
 * shows rather than by walking a menu to find it.
 */
void draw_open_window_row(const render_input& input, std::string_view instance_id,
    std::string_view identity_id) {
    constexpr std::array<std::pair<window_kind, const char*>, 3> kinds{{
        {window_kind::chart, "Open chart"},
        {window_kind::grid, "Open grid"},
        {window_kind::notice, "Open notice"},
    }};

    for (const auto& [kind, tooltip] : kinds) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(kind));
        if (action_button("open", hedgr::ui::theme::window_kind_icon(kind),
                tooltip)) {
            command request;
            request.kind = command_kind::open_window;
            request.instance_id = std::string{instance_id};
            request.identity_id = std::string{identity_id};
            request.new_window_kind = kind;
            input.commands.push(std::move(request));
        }
        ImGui::PopID();
    }
}

}

namespace hedgr::ui::render {

/**
 * @brief The Instance Manager: the root surface, and the only window that is
 * always usable.
 *
 * It has to make sense with no instance stored, no identity stored and no
 * session open, because that is the state a person meets it in on a first run.
 * Everything it can do is an icon with a tooltip, like the rest of the
 * interface, because there is no menu here to fall back on either.
 */
void render_instance_manager(window_handle handle, const render_input& input,
    window_ui_state& state) {
    /*
     * The Instance Manager is root: it ignores whether the window table thinks
     * it is open, because closing it would leave an interface with no way to
     * open anything again.
     */
    static_cast<void>(handle);

    if (hedgr::ui::render::detail::add_instance_requested) {
        hedgr::ui::render::detail::add_instance_requested = false;
        add_form_open = true;
    }

    ImGui::PushID("instance-manager");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter instances and identities",
        state.filter.data(), state.filter.size());

    if (add_form_open)
        draw_add_form(input);

    draw_instances_table(input, state);

    if (input.instances.find(selected_instance_id) != nullptr)
        draw_identities(input, state);

    if (!connect_identity_id.empty()
        && input.instances.find(selected_instance_id) != nullptr)
        draw_connect_form(input, state);

    draw_sessions_table(input);

    /*
     * A window is opened against a live session, so the icon row is offered
     * per authenticated session rather than per instance: there is nothing to
     * show for an instance nobody is signed in to.
     */
    for (const auto& row : collect_sessions(input)) {
        if (row.status != session_status::authenticated)
            continue;
        ImGui::PushID(static_cast<int>(row.handle.index));
        ImGui::TextUnformatted(instance_name(input, row.instance_id).c_str());
        draw_open_window_row(input, row.instance_id, row.identity_id);
        ImGui::PopID();
    }

    ImGui::PopID();
}

}
