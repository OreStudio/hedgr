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
using hedgr::model::domain::account;
using hedgr::model::domain::instance;
using hedgr::model::domain::label;
using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::model::state::instance_handle;
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

/** @brief The instance whose accounts are listed, empty when none is. */
std::string selected_instance_id;

/** @brief The account whose connect form is showing, empty when none is. */
std::string connect_account_id;

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
bool action_button(const char* id, const char* glyph, const char* tooltip) {
    const float height = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    const bool pressed = ImGui::Button(glyph, ImVec2(height, height));
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
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

    /** @brief Stable id of the account the session presents. */
    std::string account_id;
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
        row.account_id = std::string{sessions.account_id(handle)};
        rows.push_back(std::move(row));
    }
    return rows;
}

/** @brief The label to show for an account, falling back to its principal. */
std::string account_label(const account& row) {
    if (!row.name.empty())
        return row.name;
    return row.principal;
}

/**
 * @brief The name to show for an instance id, falling back to the id itself.
 *
 * A session may outlive the instance it points at, and a row that renders an
 * empty cell tells the reader nothing about why.
 */
std::string instance_name(const render_input& input, std::string_view id) {
    if (const auto handle = input.instances.find(id))
        return input.instances.get(*handle)->name;
    return std::string{id};
}

/** @brief The form that creates an instance, shown on request. */
void draw_add_form(const render_input& input) {
    ImGui::SeparatorText("New instance");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-name", "name", &add_draft.name);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-url", "nats://host:port",
        &add_draft.nats_url);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##add-prefix", "subject prefix",
        &add_draft.subject_prefix);

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

/**
 * @brief The labels attached to an instance, as name and colour.
 *
 * A label attached to the instance whose row has since gone is skipped rather
 * than drawn as an id: the attachment is a pair of keys, and only one of them
 * is what a person was shown.
 */
std::vector<const label*> labels_of_instance(const render_input& input,
    std::string_view instance_id) {
    std::vector<const label*> result;
    for (const auto& id : input.labellings.labels_of(
             hedgr::model::domain::labelling_owner::instance, instance_id)) {
        if (const auto handle = input.labels.find(id))
            result.push_back(input.labels.get(*handle));
    }
    return result;
}

/**
 * @brief The two probe indicators beside an instance's name, and why.
 *
 * The colour is the answer and the tooltip is the reason: a red dot tells a
 * reader the deployment does not answer, and the next question is always why,
 * which is exactly what probe_result::detail holds. Both indicators carry the
 * same detail, because it explains the pair rather than either dot on its own.
 *
 * An instance no probe has answered for is drawn grey in both, which is what
 * "unknown" means here: nobody has asked yet, so nothing has been said.
 */
void draw_probe_indicators(const render_input& input, instance_handle handle) {
    namespace icon = hedgr::ui::icon;
    namespace theme = hedgr::ui::theme;
    using hedgr::model::domain::reachability;
    using hedgr::model::domain::readiness;

    const auto* probe = input.probes.get(handle);
    const auto reach = probe != nullptr ? probe->reach : reachability::unknown;
    const auto ready = probe != nullptr ? probe->ready : readiness::unknown;
    const char* detail = probe != nullptr && !probe->detail.empty()
        ? probe->detail.c_str()
        : "no probe has answered for this instance yet";

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(theme::reachability_colour(reach)));
    ImGui::TextUnformatted(icon::probe);
    ImGui::SetItemTooltip("Reachability: %s\n%s",
        hedgr::model::domain::to_string(reach).data(), detail);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(theme::readiness_colour(ready)));
    ImGui::TextUnformatted(icon::ready);
    ImGui::SetItemTooltip("Readiness: %s\n%s",
        hedgr::model::domain::to_string(ready).data(), detail);
    ImGui::PopStyleColor();
}

/**
 * @brief The width the two indicators take, and the gap before them.
 *
 * Measured rather than guessed because the Selectable that draws the name is
 * given a width that leaves exactly this much room: a Selectable left at its
 * default spans the whole column, and the indicators drawn after it would then
 * begin past the column's right edge, where the table clips them and nobody
 * ever sees the status of anything.
 */
[[nodiscard]] float probe_indicator_width() {
    const auto glyphs = ImGui::CalcTextSize(hedgr::ui::icon::probe).x
        + ImGui::CalcTextSize(hedgr::ui::icon::ready).x;
    return glyphs + 3.0f * ImGui::GetStyle().ItemSpacing.x;
}

/**
 * @brief One instance row's label names, each in its own colour.
 *
 * The names sit beside the instance name rather than in a column of their own,
 * because a label is an attribute of the row and reading it means reading it
 * together with the name. Nothing else about a label is drawn here: the
 * severity is what the confirmation reads, and the browser that edits them is
 * a surface of its own.
 */
void draw_instance_labels(const render_input& input,
    std::string_view instance_id) {
    const auto attached = labels_of_instance(input, instance_id);
    for (std::size_t index = 0; index < attached.size(); ++index) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(
                hedgr::ui::theme::label_colour(attached[index]->colour)));
        ImGui::TextUnformatted(attached[index]->name.c_str());
        ImGui::PopStyleColor();
    }
}

void draw_instances_table(const render_input& input, window_ui_state& state) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("##instances", 3, flags, ImVec2(0.0f, 160.0f)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    /*
     * The rows span includes the holes a removal leaves behind, so the walk
     * checks the slab before reading a slot.
     */
    const auto& layout = input.instances.layout();
    const auto rows = input.instances.rows();
    for (std::size_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(static_cast<std::uint32_t>(index)))
            continue;
        const auto& row = rows[index];
        if (!filter_contains(row.name, state.filter.data())
            && !filter_contains(row.subject_prefix, state.filter.data())
            && !filter_contains(row.nats_url, state.filter.data()))
            continue;

        ImGui::TableNextRow();
        ImGui::PushID(row.id.c_str());

        ImGui::TableNextColumn();
        const bool selected = selected_instance_id == row.id;

        /*
         * The name gives up exactly the room the two indicators need, so that
         * they land inside the column rather than past its right edge, and the
         * labels follow them.
         */
        const float name_width =
            ImGui::GetContentRegionAvail().x - probe_indicator_width();
        if (ImGui::Selectable(row.name.c_str(), selected,
                ImGuiSelectableFlags_None, ImVec2(name_width, 0.0f)))
            selected_instance_id = selected ? std::string{} : row.id;
        ImGui::SetItemTooltip("%s\n%s", row.name.c_str(), row.nats_url.c_str());

        draw_probe_indicators(input,
            instance_handle{static_cast<std::uint32_t>(index),
                layout.generation[index]});
        draw_instance_labels(input, row.id);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.subject_prefix.c_str());

        ImGui::TableNextColumn();
        if (action_button("edit", hedgr::ui::icon::edit, "Edit instance"))
            selected_instance_id = row.id;
        ImGui::SameLine();
        if (action_button("remove", hedgr::ui::icon::remove,
                "Remove instance")) {
            command request;
            request.kind = command_kind::remove_instance;
            request.instance_id = row.id;
            input.commands.push(std::move(request));
        }
        ImGui::SameLine();
        if (action_button("connect", hedgr::ui::icon::connect,
                "Connect an account")) {
            selected_instance_id = row.id;
            state.selected_row = -1;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void draw_accounts(const render_input& input, window_ui_state& state) {
    ImGui::SeparatorText("Accounts");

    const auto children = input.accounts.children_of(selected_instance_id);
    if (children.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("No accounts on this instance yet.");
        ImGui::PopStyleColor();
        return;
    }

    const float reserved = ImGui::GetFrameHeight() * 2.0f
        + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const account& row = *children[index];
        if (!filter_contains(account_label(row), state.filter.data())
            && !filter_contains(row.principal, state.filter.data()))
            continue;

        ImGui::PushID(row.id.c_str());
        const bool selected = state.selected_row == static_cast<int>(index);
        if (ImGui::Selectable(account_label(row).c_str(), selected,
                ImGuiSelectableFlags_None,
                ImVec2(ImGui::GetContentRegionAvail().x - reserved, 0.0f)))
            state.selected_row = selected ? -1 : static_cast<int>(index);
        ImGui::SetItemTooltip("%s", row.principal.c_str());

        ImGui::SameLine();
        if (action_button("connect", hedgr::ui::icon::connect,
                "Connect this account")) {
            connect_account_id = row.id;
            state.draft_principal = row.principal;
        }
        ImGui::SameLine();
        if (action_button("remove", hedgr::ui::icon::remove,
                "Remove account")) {
            command request;
            request.kind = command_kind::remove_account;
            request.instance_id = selected_instance_id;
            request.account_id = row.id;
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
        request.account_id = connect_account_id;
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
        connect_account_id.clear();
    }
}

void draw_sessions_table(const render_input& input) {
    ImGui::SeparatorText("Sessions");

    const auto rows = collect_sessions(input);
    if (rows.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("No sessions. Connect an account to start.");
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
    ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthStretch);
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
        if (const auto found = input.accounts.find(row.account_id)) {
            const auto* account_row = input.accounts.get(*found);
            ImGui::TextUnformatted(account_label(*account_row).c_str());
        } else {
            ImGui::TextUnformatted(row.account_id.c_str());
        }

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
            request.account_id = row.account_id;
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
    std::string_view account_id) {
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
            request.account_id = std::string{account_id};
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
 * It has to make sense with no instance stored, no account stored and no
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
    ImGui::InputTextWithHint("##filter", "filter instances and accounts",
        state.filter.data(), state.filter.size());

    if (add_form_open)
        draw_add_form(input);

    draw_instances_table(input, state);

    const bool instance_selected =
        input.instances.find(selected_instance_id).has_value();
    if (instance_selected)
        draw_accounts(input, state);

    if (!connect_account_id.empty() && instance_selected)
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
        draw_open_window_row(input, row.instance_id, row.account_id);
        ImGui::PopID();
    }

    ImGui::PopID();
}

}
