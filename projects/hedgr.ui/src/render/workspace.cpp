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
 * @brief What the lock state means, for the indicator's tooltip.
 *
 * Prose rather than the enum's own spelling, because a tooltip is read by a
 * person deciding what to do next: "absent" tells them nothing and "no master
 * password has been created yet" tells them everything. The state's own name
 * travels in front of it so a reader can match the tooltip to a log line.
 */
std::string lock_tooltip(hedgr::model::domain::store_lock value) {
    using hedgr::model::domain::store_lock;
    switch (value) {
    case store_lock::absent:
        return std::format("Store {}: no master password has been created yet",
            hedgr::model::domain::to_string(value));
    case store_lock::locked:
        return std::format("Store {}: the master password is not held; unlock "
                           "it to read the store",
            hedgr::model::domain::to_string(value));
    case store_lock::unlocked:
        return std::format("Store {}: the master password is held for this run",
            hedgr::model::domain::to_string(value));
    case store_lock::failed:
        return std::format("Store {}: the last master password was refused",
            hedgr::model::domain::to_string(value));
    }
    return "Store: unknown lock state";
}

/**
 * @brief The icon strip along the top: the whole of the application's chrome.
 *
 * Add instance, refresh, probe, the lock indicator, full screen and exit. The
 * lock indicator is text rather than a button so that it cannot be mistaken for
 * the control; the control is the padlock action beside it.
 */
void draw_toolbar(const hedgr::ui::render::render_input& input, bool& lock_clicked,
    bool& unlock_clicked) {
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

    /*
     * Every action here reaches the store, so none of them is drawn while the
     * lock is closed: a button that cannot do its job is worse than no button.
     * The lock control itself is below, outside this gate, because it is the
     * one action that is available while locked.
     */
    const bool open = input.store_lock == hedgr::model::domain::store_lock::unlocked;

    if (!open)
        ImGui::BeginDisabled();

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
        const auto open_column = input.windows.open_column();
        for (std::size_t index = 0; index < layout.alive.size(); ++index) {
            if (layout.alive[index] == 0 || open_column[index] == 0)
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
    if (icon_button("probe", icon::probe, "Probe every instance now")) {
        /*
         * One command for the whole set rather than one per instance: the
         * runtime owns the schedule, so asking it to bring every deadline
         * forward leaves it to decide what is already being probed and in what
         * order the rest follow.
         */
        hedgr::model::command::command request;
        request.kind = command_kind::probe_all;
        input.commands.push(std::move(request));
    }

    if (!open)
        ImGui::EndDisabled();

    /*
     * The lock indicator. Coloured, because the colour is what is read at a
     * glance, and tooltipped with prose, because the glyph alone does not say
     * which of the three closed-looking states this is.
     */
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                           hedgr::ui::theme::store_lock_colour(input.store_lock)),
        "%s", hedgr::ui::theme::store_lock_icon(input.store_lock));
    ImGui::SetItemTooltip("%s", lock_tooltip(input.store_lock).c_str());

    ImGui::SameLine();
    if (open) {
        if (icon_button("lock", icon::lock,
                "Lock the store, forgetting the master password for this run"))
            lock_clicked = true;
    } else {
        if (icon_button("unlock", icon::unlock, "Enter the master password"))
            unlock_clicked = true;
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

    /*
     * The gate. While the store is not unlocked there is nothing behind any of
     * these windows to draw -- every read of the store is a decryption -- so
     * the prompt is the only surface, and no window is drawn and no refresh is
     * requested. This is the whole of the "lock gates the interface" rule.
     */
    if (!shows_instances(input)) {
        draw_lock_prompt(input);
        return;
    }

    /*
     * The gate reopened, so the prompt is finished with. Its typed buffer is
     * wiped rather than kept, and it is armed again for the next lock, so a
     * password is never sitting in this object while the store is open.
     */
    lock_prompt_dismissed_ = false;
    clear_password_buffers();

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
         * The visible title can change with the data, so the ImGui id
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

void workspace::draw_toolbar(const render_input& input) {
    bool lock_clicked = false;
    bool unlock_clicked = false;
    ::draw_toolbar(input, lock_clicked, unlock_clicked);

    if (lock_clicked) {
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::lock_store;
        input.commands.push(std::move(request));
    }

    /*
     * Pressing unlock re-arms the prompt. It is the one way back in after a
     * dismissal, which is what keeps "not now" from being permanent.
     */
    if (unlock_clicked)
        lock_prompt_dismissed_ = false;
}

bool workspace::prompt_visible(const render_input& input) const noexcept {
    using hedgr::model::domain::store_lock;

    /*
     * Never while the store is open: there is nothing to ask for. Never after a
     * dismissal, so the operator is not asked again this run.
     *
     * The prompt does stay up while a derivation is running, and that is the
     * point rather than an oversight: the key derivation is slow on purpose, and
     * a modal that disappears for half a second leaves an operator looking at a
     * background wondering whether the application has hung. It shows that it is
     * working instead.
     */
    if (input.store_lock == store_lock::unlocked)
        return false;
    return !lock_prompt_dismissed_;
}

void workspace::clear_password_buffers() noexcept {
    /*
     * std::fill and not a memset: the buffers are arrays of char and the
     * standard algorithm is what the rest of this file uses. What matters is
     * that the bytes are gone, not how they went.
     */
    std::fill(master_password_.begin(), master_password_.end(), '\0');
    std::fill(master_password_again_.begin(), master_password_again_.end(), '\0');
}

void workspace::submit_master_password(const render_input& input) {
    using hedgr::model::command::command_kind;
    using hedgr::model::domain::store_lock;

    hedgr::model::command::command request;
    request.kind = input.store_lock == store_lock::absent
        ? command_kind::set_master_password
        : command_kind::unlock_store;
    request.password = std::string{master_password_.data()};
    input.commands.push(std::move(request));

    /*
     * The buffers are wiped the moment the command holds the password, so the
     * typed copy lives for one frame and the command's copy is the runtime's to
     * clear. The eye toggle is reset with them: a reveal that survived the
     * password it revealed would be showing the next one.
     */
    clear_password_buffers();
    master_password_visible_ = false;
    create_mismatch_ = false;
}

void workspace::draw_lock_prompt(const render_input& input) {
    using hedgr::model::domain::store_lock;

    if (!prompt_visible(input))
        return;

    const bool creating = input.store_lock == store_lock::absent;

    /*
     * A fixed size, no resize, no collapse and no scrolling, matching every
     * other dialog in this interface. Fixed because a password box that can be
     * resized invites a size that hides the eye toggle, and because a modal
     * whose layout moves is a modal that can be made to look like something
     * else.
     */
    /*
     * Fixed, and sized to what is in it. The create form is taller because it
     * asks twice and explains what the password is for; the unlock form asks
     * once and says one line. The sentence below is wrapped rather than run off
     * the edge, because a window this width cannot carry it on one line and a
     * clipped explanation is worse than none.
     */
    const ImVec2 size(460.0f, creating ? 220.0f : 150.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = creating ? "Create the master password###hedgr-lock"
                                 : "Unlock the store###hedgr-lock";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    if (creating) {
        ImGui::TextWrapped(
            "The whole local store sits behind this password. It cannot be "
            "recovered, and losing it loses the instances and folders as well "
            "as the stored credentials.");
    } else {
        ImGui::TextWrapped("Enter the master password to read the store.");
    }

    ImGui::Spacing();

    /*
     * The reveal toggle sits immediately right of the password box, because
     * that is where an eye is looked for. Same construction as the connect
     * form's, so the two read as one control used twice rather than two
     * controls that happen to look alike.
     */
    const float eye = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-(eye + ImGui::GetStyle().ItemInnerSpacing.x));
    ImGui::InputTextWithHint("##master-password", "master password",
        master_password_.data(), master_password_.size(),
        master_password_visible_ ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    const float height = ImGui::GetFrameHeight();
    ImGui::PushID("reveal");
    if (ImGui::Button(master_password_visible_ ? hedgr::ui::icon::eye_off
                                               : hedgr::ui::icon::eye,
            ImVec2(height, height)))
        master_password_visible_ = !master_password_visible_;
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", master_password_visible_ ? "Hide password"
                                                         : "Show password");

    if (creating) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##master-password-again", "master password again",
            master_password_again_.data(), master_password_again_.size(),
            master_password_visible_ ? 0 : ImGuiInputTextFlags_Password);

        /*
         * Checked as it is typed rather than on submit, because the failure is
         * a typo and the operator can fix it while they are still looking at
         * the second field.
         */
        create_mismatch_ = master_password_again_[0] != '\0'
            && std::string_view{master_password_.data()}
                != std::string_view{master_password_again_.data()};
        if (create_mismatch_)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                                   hedgr::ui::theme::store_lock_colour(
                                       store_lock::failed)),
                "The two passwords do not match.");
    }

    /*
     * The failure message the runtime sent, inline beside the prompt rather
     * than in a banner: it is about this field, and it belongs next to it.
     */
    if (!input.lock_message.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                               hedgr::ui::theme::store_lock_colour(store_lock::failed)),
            "%s", input.lock_message.c_str());
    }

    ImGui::Spacing();

    const bool typed = master_password_[0] != '\0';
    const bool complete = creating
        ? typed && master_password_again_[0] != '\0' && !create_mismatch_
        : typed;

    if (input.unlocking) {
        /*
         * Working, not frozen. The fields above are empty because the buffers
         * were wiped on submit, so there is nothing to edit and nothing to
         * press: the only thing to show is that the derivation is running.
         */
        ImGui::TextUnformatted("Deriving the key...");
    } else {
        if (!complete)
            ImGui::BeginDisabled();
        const bool confirmed = ImGui::Button(creating ? "Create" : "Unlock");
        if (!complete)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            /*
             * Cancelling changes nothing but the prompt. The lock is left
             * exactly where it was, which is the whole point of allowing a
             * cancel: an operator who does not have the password right now must
             * be able to put the prompt down and come back to it.
             */
            clear_password_buffers();
            master_password_visible_ = false;
            create_mismatch_ = false;
            lock_prompt_dismissed_ = true;
            ImGui::CloseCurrentPopup();
        }

        if (confirmed && complete)
            submit_master_password(input);
    }

    ImGui::EndPopup();
}

}