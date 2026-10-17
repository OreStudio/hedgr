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
#include "hedgr.ui/render/windows.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/panels.hpp"
#include "imgui.h"
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;
using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::session_status;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;
using hedgr::ui::widget::session_facts;

/** @brief One labelled fact of the session, as a row of the facts table. */
void fact_row(const char* glyph, const char* name, std::string_view value) {
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", glyph);
    ImGui::SetItemTooltip("%s", name);

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);

    ImGui::TableNextColumn();
    if (value.empty()) {
        /*
         * An empty fact says so in words rather than as a blank cell: a blank
         * cell beside a heading reads as a value that failed to load, and the
         * truth here is usually that IAM has not named one yet.
         */
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("not reported");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

}

namespace hedgr::ui::render {

void render_workspace_window(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    /*
     * Only the Instances window carries the status line today. The parameter
     * is here because every renderer takes it, so a window that later has
     * something to announce needs no change to the frame.
     */
    static_cast<void>(status);
    static_cast<void>(state);

    /*
     * A workspace is always bound to a session, so the facts come from the
     * window's binding rather than from its state: the account is in the state
     * and the instance is in the binding, which is one place for the pair to be
     * rather than two.
     */
    const auto session = input.windows.session_of(handle);
    const auto& shape = input.windows.state(handle);
    const auto* workspace = std::get_if<hedgr::model::domain::workspace_state>(&shape);
    const std::string account_id =
        workspace != nullptr ? workspace->account_id : std::string{};

    ImGui::PushID("workspace");
    hedgr::ui::render::draw_identity_bar(
        hedgr::ui::render::identity_of(input, handle));

    const auto facts = session
        ? widget::collect_session_facts(input.sessions, input.accounts, *session)
        : session_facts{};

    /*
     * A workspace outlives the session it points at, and renders the loss
     * rather than closing. A window that vanished when a connection dropped
     * would take with it the only surface that could say what went wrong.
     */
    if (!session || !facts.found) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(
                theme::status_colour(session_status::disconnected)));
        ImGui::TextUnformatted(icon::session_failed);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted("This session has ended. Sign in again from the "
                               "Accounts window.");

        if (widget::icon_button("close", icon::cancel, "Close this workspace")) {
            command request;
            request.kind = command_kind::close_window;
            request.window_id = std::string{input.windows.id(handle)};
            input.commands.push(std::move(request));
        }
        ImGui::PopID();
        return;
    }

    const auto instance_name = [&]() -> std::string {
        if (const auto found = input.instances.find(facts.instance_id))
            return input.instances.get(*found)->name;
        return facts.instance_id.empty() ? std::string{"unknown instance"}
                                         : facts.instance_id;
    }();

    /*
     * The sign-out action is the window's own, and it is drawn in the worked
     * area rather than as a bare glyph: it is the one thing here that changes
     * anything, and it is the way out.
     */
    if (widget::icon_button("refresh", icon::refresh,
            "Ask the session for fresh data now")) {
        command request;
        request.kind = command_kind::refresh_window;
        request.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(request));
    }

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Sign out", icon::sign_out).c_str())) {
        command request;
        request.kind = command_kind::disconnect_session;
        request.instance_id = facts.instance_id;
        request.account_id = facts.account_id;
        input.commands.push(std::move(request));

        /*
         * The window closes with the session, which is what "signing out closes
         * the Workspace" means in code. The runtime also unbinds every window
         * of the session, so a window that somehow outlives this command
         * renders the session-loss state rather than stale facts.
         */
        command close;
        close.kind = command_kind::close_window;
        close.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(close));
    }
    ImGui::SetItemTooltip("%s", "Sign out and close this workspace");

    ImGui::Separator();

    const auto status_name =
        std::string{hedgr::model::domain::to_string(facts.status)};
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(theme::status_colour(facts.status)));
    ImGui::Text("%s  %.*s", theme::status_icon(facts.status),
        static_cast<int>(status_name.size()), status_name.data());
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("%s", instance_name.c_str());

    /*
     * The same disappearing open state as the Instances window's dashboard: a
     * CollapsingHeader overwrites the bool it is handed from its own storage,
     * so the panel is opened here rather than by the window state's initialiser.
     */
    ImGui::SetNextItemOpen(
        widget::panel_default_open(widget::panel_kind::session_facts),
        ImGuiCond_Appearing);
    if (!ImGui::CollapsingHeader(std::format("{} Session", icon::clock).c_str(),
            &state.panel_open)) {
        ImGui::PopID();
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTable("##workspace-facts", 3, flags)) {
        ImGui::TableSetupColumn("##glyph", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Fact", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        fact_row(icon::instance, "Instance", instance_name);
        fact_row(icon::account, "Principal", facts.principal);
        fact_row(icon::session, "Account", facts.account_name);
        fact_row(icon::fact, "Tenant", facts.tenant);
        fact_row(icon::fact, "Party", facts.party);

        /*
         * The latency is the last round trip the session reported, in
         * microseconds. It is the only measurement Hedgr has of the connection,
         * and it is a fact rather than a judging: nothing here says whether the
         * number is good.
         */
        fact_row(icon::clock, "Last round trip",
            facts.latency_us > 0
                ? std::format("{:.1f} ms",
                      static_cast<double>(facts.latency_us) / 1000.0)
                : std::string{});

        /*
         * The session's own clock reading, rendered as an age rather than as a
         * timestamp: the monotonic clock has no wall time in it, and "38
         * seconds ago" is the question a person is actually asking when they
         * look at when a session started.
         */
        fact_row(icon::clock, "Established",
            facts.opened_at_ns > 0 && input.now_ns >= facts.opened_at_ns
                ? std::format("{:.0f} s ago",
                      static_cast<double>(input.now_ns - facts.opened_at_ns)
                          / 1.0e9)
                : std::string{});

        ImGui::EndTable();
    }

    ImGui::Spacing();

    /*
     * The permission-driven panels are not here yet, and this says so rather
     * than leaving a gap a reader would take for an oversight.
     *
     * The permissions a Workspace shows depend on what the signed-in account
     * may do, and that source does not exist: no component in this tree asks
     * IAM what an account is allowed to do, and inventing a permission list
     * here would put a plausible-looking screen in front of an operator with
     * nothing behind it. What is shown above is every fact the runtime actually
     * holds about this session. The panels land here when there is something
     * true to put in them.
     */
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SeparatorText(std::format("{} Permissions", icon::panel).c_str());
    ImGui::TextWrapped(
        "Reserved. The panels here will be driven by what this account is "
        "allowed to do, and Hedgr cannot ask that question yet: there is no "
        "permission source in the deployment's protocol surface that this "
        "client speaks. Nothing is shown rather than something invented.");
    ImGui::PopStyleColor();

    ImGui::PopID();
}

}
