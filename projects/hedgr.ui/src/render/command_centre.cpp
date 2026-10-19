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
#include "hedgr.model/domain/deployment.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "ores.logging/make_logger.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/** @brief This window's logger. Gates and screen changes are reported once. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance =
        make_logger("hedgr.ui.render.command_centre");
    return instance;
}

/**
 * @brief The screens this window can host, in the order a menu offers them.
 *
 * A table rather than a chain of ifs, because the tab strip and the menus both
 * read the same set and a screen added to one and forgotten in the other would
 * be a tab nobody can open.
 */
struct screen_entry final {
    const char* label;
    std::string_view id;
};

constexpr screen_entry screen_table[] = {
    {"Overview", "overview"},
    {"Tenants", "tenants"},
};

/**
 * @brief A screen a capture asks for, applied to the first window drawn.
 *
 * A capture has no pointer, so it cannot work a menu. What this writes is the
 * same scratch the menu writes, and it is consumed on the frame it is applied,
 * so an ordinary run never reads it.
 */
std::string pending_capture_screen;

/** @brief The label of a screen, or its id when it is not in the table. */
[[nodiscard]] std::string screen_label(std::string_view id) {
    for (const auto& entry : screen_table) {
        if (entry.id == id)
            return entry.label;
    }
    return std::string{id};
}

/**
 * @brief Opens a screen, selecting it if it is already a tab.
 *
 * The tab is appended once and only focused afterwards, so asking for the same
 * screen twice is one tab rather than two.
 */
void open_screen(hedgr::ui::render::command_centre_ui_state& state,
    std::string_view id) {
    const auto found = std::find(state.screens.begin(), state.screens.end(), id);
    if (found == state.screens.end())
        state.screens.emplace_back(id);
    state.active = std::string{id};
    state.select_next = true;
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Command centre screen '" << id << "' opened";
}

/** @brief Opens a screen from a menu item, which is the click path. */
void menu_screen(hedgr::ui::render::command_centre_ui_state& state,
    const char* label, std::string_view id) {
    if (ImGui::MenuItem(label))
        open_screen(state, id);
}

/** @brief The colour a tenant's lifecycle status is drawn in. */
[[nodiscard]] ImU32 status_colour(std::string_view status) {
    if (status == "active")
        return theme::status_colour(domain::session_status::authenticated);
    if (status == "bootstrapping")
        return theme::readiness_colour(domain::readiness::bootstrapping);
    if (status == "suspended")
        return theme::failure_colour;
    return ImGui::GetColorU32(ImGuiCol_TextDisabled);
}

/**
 * @brief The deployment's bootstrap answer, when a probe has one.
 *
 * Absent means no probe has answered yet, which is not the same as an answer
 * of false: a window that treated the two alike would claim the deployment is
 * set up before anybody asked.
 */
[[nodiscard]] const domain::bootstrap_status* bootstrap_of(
    const render_input& input, hedgr::model::state::instance_handle instance) {
    const auto* probe = input.probes.get(instance);
    if (probe == nullptr || !probe->bootstrap)
        return nullptr;
    return &*probe->bootstrap;
}

/**
 * @brief The card that says the deployment has no administrator yet.
 *
 * The window shows this and nothing else. Every count would be zero, and a
 * screen of zeros reads as an estate with nothing in it rather than as a
 * deployment nobody has set up, which are two different instructions.
 */
void draw_bootstrap_card(const render_input& input,
    std::string_view instance_id, std::string_view instance_name) {
    const float pane_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const int columns = widget::card_columns(pane_width);
    const float width = widget::card_width(pane_width, columns);
    const float height = widget::card_height();

    std::vector<widget::card_spec> cards;
    cards.push_back(widget::card_spec{.id = "cc-bootstrap",
        .glyph = icon::warning,
        .title = "Not bootstrapped",
        .value = "This deployment has no administrator yet.",
        .caption =
            "Create the first administrator in the ORE web interface, then "
            "reopen this window.",
        .colour = theme::failure_colour,
        .full_width = true,
        .wrap = true});
    cards.push_back(widget::card_spec{.id = "cc-bootstrap-instance",
        .glyph = icon::instance,
        .title = "Deployment",
        .value = std::string{instance_name},
        .caption = "the deployment whose setup is incomplete"});

    widget::topic_spec topic;
    topic.title = "Command centre";
    topic.cards = std::move(cards);
    std::vector<widget::topic_spec> topics;
    topics.push_back(std::move(topic));

    widget::draw_topics(topics, pane_width, width, height, columns);

    ImGui::Spacing();
    if (ImGui::Button("Recheck")) {
        command request;
        request.kind = command_kind::probe_instance;
        request.instance_id = std::string{instance_id};
        input.commands.push(std::move(request));
    }
    ImGui::SetItemTooltip("%s",
        "Ask the deployment again whether it has been set up");
}

/**
 * @brief The Overview: the counts the dashboard reports, as a wall of cards.
 *
 * The counts come from one snapshot, so they are all about the same moment. The
 * failure state is drawn instead of the counts rather than beside them, because
 * a zero beside a failure reads as a measurement.
 */
void draw_overview(const render_input& input, const domain::deployment_snapshot& snapshot,
    std::string_view window_id, std::string_view instance_name) {
    const float pane_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const int columns = widget::card_columns(pane_width);
    const float width = widget::card_width(pane_width, columns);
    const float height = widget::card_height();

    if (!snapshot.failure.empty()) {
        std::vector<widget::topic_spec> topics;
        widget::topic_spec failure;
        failure.title = "Deployment";
        failure.cards.push_back(widget::card_spec{.id = "cc-failure",
            .glyph = icon::error,
            .title = "The deployment could not be read",
            .value = snapshot.failure,
            .caption = std::format("while reading {}", snapshot.failing_read),
            .colour = theme::failure_colour,
            .full_width = true,
            .wrap = true});
        topics.push_back(std::move(failure));
        widget::draw_topics(topics, pane_width, width, height, columns);

        if (ImGui::Button("Read again")) {
            command request;
            request.kind = command_kind::refresh_window;
            request.window_id = std::string{window_id};
            input.commands.push(std::move(request));
        }
        return;
    }

    if (!snapshot.is_read()) {
        ImGui::TextDisabled("%s", "Reading the deployment...");
        return;
    }

    std::vector<widget::topic_spec> topics;

    widget::topic_spec tenants;
    tenants.title = "Tenants";
    tenants.cards.push_back(widget::card_spec{.id = "cc-tenants-in-service",
        .glyph = icon::ready,
        .title = "In service",
        .value = std::to_string(snapshot.in_service),
        .caption = "active and running",
        .colour = theme::status_colour(domain::session_status::authenticated)});
    tenants.cards.push_back(widget::card_spec{.id = "cc-tenants-evaluation",
        .glyph = icon::fact,
        .title = "On evaluation",
        .value = std::to_string(snapshot.on_evaluation),
        .caption = "evaluation tenants"});
    tenants.cards.push_back(widget::card_spec{.id = "cc-tenants-setting-up",
        .glyph = icon::probe,
        .title = "Setting up",
        .value = std::to_string(snapshot.setting_up),
        .caption = "bootstrapping",
        .colour = theme::readiness_colour(domain::readiness::bootstrapping)});
    tenants.cards.push_back(widget::card_spec{.id = "cc-tenants-attention",
        .glyph = icon::warning,
        .title = "Needs attention",
        .value = std::to_string(snapshot.needing_attention),
        .caption = "suspended",
        .colour = snapshot.needing_attention > 0
            ? theme::failure_colour
            : ImGui::GetColorU32(ImGuiCol_TextDisabled)});
    tenants.cards.push_back(widget::card_spec{.id = "cc-tenants-total",
        .glyph = icon::instance,
        .title = "In all",
        .value = std::to_string(snapshot.tenant_total),
        .caption = "every tenant"});
    topics.push_back(std::move(tenants));

    widget::topic_spec deployment;
    deployment.title = "Deployment";
    deployment.cards.push_back(widget::card_spec{.id = "cc-instance",
        .glyph = icon::instance,
        .title = "Instance",
        .value = std::string{instance_name}});
    deployment.cards.push_back(widget::card_spec{.id = "cc-observed",
        .glyph = icon::clock,
        .title = "Read",
        .value = std::format("{:.0f} s ago",
            static_cast<double>(input.now_ns - snapshot.observed_at_ns) / 1.0e9),
        .caption = "one snapshot, all counts"});
    topics.push_back(std::move(deployment));

    widget::draw_topics(topics, pane_width, width, height, columns);
}

/** @brief The Tenants screen: the roster the overview counts. */
void draw_tenants(const domain::deployment_snapshot& snapshot) {
    if (!snapshot.failure.empty()) {
        ImGui::TextWrapped("%s", snapshot.failure.c_str());
        return;
    }
    if (!snapshot.is_read()) {
        ImGui::TextDisabled("%s", "Reading the deployment...");
        return;
    }

    /*
     * The roster's own size, above the rows rather than under them. A line
     * beneath a table that fills the pane lands on the foot's rule, and the
     * count is the first thing a reader wants, not the last.
     */
    const std::string summary =
        snapshot.tenants.size() < snapshot.tenant_total
        ? std::format("Showing {} of {} tenants.", snapshot.tenants.size(),
              snapshot.tenant_total)
        : snapshot.tenant_total == 1
        ? std::string{"1 tenant."}
        : std::format("{} tenants.", snapshot.tenant_total);
    ImGui::TextDisabled("%s", summary.c_str());
    ImGui::Spacing();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_ScrollY;

    const float height = -ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("##cc-tenants", 3, flags, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
            120.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
            140.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const auto& row : snapshot.tenants) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                                   status_colour(row.status)),
                "%s", row.status.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.type.c_str());
        }
        ImGui::EndTable();
    }

}

} // namespace

namespace hedgr::ui::render {

void open_command_centre_screen_for_capture(std::string_view screen) {
    pending_capture_screen = std::string{screen};
}

void render_command_centre_window(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    ImGui::PushID("command-centre");

    const auto& shape = input.windows.state(handle);
    const auto* centre = std::get_if<domain::command_centre_state>(&shape);
    const std::string instance_id =
        centre != nullptr ? centre->instance_id : std::string{};

    const auto instance = input.instances.find(instance_id);
    const std::string instance_name = instance
        ? input.instances.get(*instance)->name : instance_id;

    auto& scratch = state.command_centre;

    /*
     * A capture's screen is the scratch a menu click writes, applied on the
     * first window that draws and then forgotten, so a run with no script never
     * reads it.
     */
    if (!pending_capture_screen.empty()) {
        open_screen(scratch, pending_capture_screen);
        pending_capture_screen.clear();
    }

    const auto session = input.windows.session_of(handle);
    const bool live = session
        && input.sessions.status(*session)
            == domain::session_status::authenticated
        && input.sessions.token(*session).size() > 0;

    widget::foot_state foot;
    foot.identity = hedgr::ui::render::identity_text(
        hedgr::ui::render::identity_of(input, handle));
    if (!instance_id.empty()) {
        const auto* probe = instance ? input.probes.get(*instance) : nullptr;
        if (probe != nullptr) {
            foot.word = theme::probe_state_word(*probe, true);
            foot.colour = theme::probe_state_colour(*probe, true);
        } else {
            foot.word = "unknown";
            foot.colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        }
    }

    /*
     * A window that outlived its session says so and stops. The alternative,
     * drawing the last snapshot, would show counts that are still true of the
     * deployment but are no longer being kept up to date, and a reader would
     * have no way to tell.
     */
    if (!live) {
        if (!scratch.session_loss_reported) {
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Command centre for '" << instance_name
                << "' has no signed-in session";
            scratch.session_loss_reported = true;
        }
        ImGui::TextWrapped("%s",
            "This command centre is not signed in. Sign in from the Accounts "
            "window to read this deployment.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    scratch.session_loss_reported = false;

    const auto* bootstrap = instance ? bootstrap_of(input, *instance) : nullptr;

    /*
     * The bootstrap gate comes before the menus, so a deployment with no
     * administrator shows one sentence and nothing that would open a screen
     * against a deployment that cannot answer for itself.
     */
    if (bootstrap != nullptr && bootstrap->in_bootstrap_mode) {
        if (!scratch.bootstrap_reported) {
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Command centre for '" << instance_name
                << "' is showing the bootstrap card";
            scratch.bootstrap_reported = true;
        }
        draw_bootstrap_card(input, instance_id, instance_name);
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }
    scratch.bootstrap_reported = false;

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Deployment")) {
            menu_screen(scratch, "Overview", "overview");
            menu_screen(scratch, "Tenants", "tenants");
            ImGui::Separator();
            if (ImGui::MenuItem("Accounts")) {
                command request;
                request.kind = command_kind::open_window;
                request.instance_id = instance_id;
                request.new_window_kind = domain::window_kind::accounts;
                input.commands.push(std::move(request));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Market data")) {
            if (ImGui::MenuItem("Synthetic series...")) {
                command request;
                request.kind = command_kind::open_window;
                request.instance_id = instance_id;
                request.account_id =
                    std::string{input.sessions.account_id(*session)};
                request.new_window_kind = domain::window_kind::series_browser;
                input.commands.push(std::move(request));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Reference data")) {
            ImGui::MenuItem("Currencies", nullptr, false, false);
            ImGui::TextDisabled("Not built yet");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Operations")) {
            ImGui::MenuItem("Services", nullptr, false, false);
            ImGui::TextDisabled("Not built yet");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Refresh now")) {
                command request;
                request.kind = command_kind::refresh_window;
                request.window_id = std::string{input.windows.id(handle)};
                input.commands.push(std::move(request));
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    /*
     * The tab strip is the working set, and the menu is the index. A tab is
     * removed only after the strip has been walked, because removing one while
     * ImGui is drawing the bar would move the ids under it.
     */
    if (scratch.screens.empty())
        scratch.screens.emplace_back("overview");

    const auto& snapshot = input.sessions.deployment(*session);

    std::vector<std::string> closing;
    if (ImGui::BeginTabBar("##cc-tabs",
            ImGuiTabBarFlags_AutoSelectNewTabs
                | ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (const auto& screen : scratch.screens) {
            const bool pinned = screen == "overview";
            const ImGuiTabItemFlags flags =
                scratch.select_next && scratch.active == screen
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None;

            bool open = true;
            const std::string label = screen_label(screen);
            if (ImGui::BeginTabItem(label.c_str(), pinned ? nullptr : &open,
                    flags)) {
                if (screen == "tenants")
                    draw_tenants(snapshot);
                else
                    draw_overview(input, snapshot,
                        input.windows.id(handle), instance_name);
                ImGui::EndTabItem();
            }
            if (!open)
                closing.push_back(screen);
        }
        ImGui::EndTabBar();
    }
    scratch.select_next = false;

    for (const auto& screen : closing) {
        if (screen == "overview")
            continue;
        const auto found =
            std::find(scratch.screens.begin(), scratch.screens.end(), screen);
        if (found != scratch.screens.end())
            scratch.screens.erase(found);
    }

    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
