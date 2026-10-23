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

#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/render/mini_plot.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/password_meter.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

/**
 * @brief The window a capture asked to see on top, or empty.
 *
 * Read once, because the environment does not change while the process runs. An
 * empty answer matches no window, which is what an ordinary run has.
 */
[[nodiscard]] std::string_view capture_focus_id() {
    static const std::string id = [] {
        const char* named = std::getenv("HEDGR_CAPTURE_FOCUS");
        return named != nullptr ? std::string{named} : std::string{};
    }();
    return id;
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
 * @brief The node a window docked by its Dock button goes into.
 *
 * The desk's free centre while nothing is docked there, so the first window
 * docked takes the room. After that, half of the tile focused last, split
 * along its longer side, so docking charts one after another builds a grid
 * rather than a stack of tabs.
 *
 * Called before the dock space is drawn, because the dock builder's splits are
 * meant to land before the node they split is submitted.
 */
[[nodiscard]] ImGuiID dock_target(ImGuiID space) {
    ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(space);
    if (centre == nullptr)
        return space;
    if (centre->Windows.empty())
        return centre->ID;

    const ImGuiDockNode* root = ImGui::DockBuilderGetNode(space);
    ImGuiDockNode* last = ImGui::DockBuilderGetNode(root->LastFocusedNodeId);
    const ImGuiDockNode* tile = last != nullptr && last->IsLeafNode() ? last
                                                                       : centre;
    ImGuiID half = 0;
    static_cast<void>(ImGui::DockBuilderSplitNode(tile->ID,
        tile->Size.x >= tile->Size.y ? ImGuiDir_Right : ImGuiDir_Down, 0.5f,
        &half, nullptr));
    return half;
}

/**
 * @brief Where a popped-out window floats: over the tile it left, kept inside
 * the work area.
 */
[[nodiscard]] ImVec2 popped_position(ImVec2 at, ImVec2 size,
    const ImGuiViewport& viewport) {
    const ImVec2 low = viewport.WorkPos;
    const ImVec2 high(viewport.WorkPos.x + viewport.WorkSize.x - size.x,
        viewport.WorkPos.y + viewport.WorkSize.y - size.y);
    return ImVec2(std::clamp(at.x, low.x, std::max(low.x, high.x)),
        std::clamp(at.y, low.y, std::max(low.y, high.y)));
}

/**
 * @brief The size a window floats at when it leaves the desk.
 *
 * What it last floated at, or @p fallback when it has floated nowhere this
 * run, and never larger than the work area.
 */
[[nodiscard]] ImVec2 floating_size_for(ImVec2 last, ImVec2 fallback,
    const ImGuiViewport& viewport) {
    const ImVec2 wanted = last.x > 0.0f ? last : fallback;
    return ImVec2(std::min(wanted.x, viewport.WorkSize.x),
        std::min(wanted.y, viewport.WorkSize.y));
}

/** @brief Whether any window is docked in the tree under @p node. */
[[nodiscard]] bool holds_windows(const ImGuiDockNode* node) {
    return node != nullptr
        && (!node->Windows.empty() || holds_windows(node->ChildNodes[0])
            || holds_windows(node->ChildNodes[1]));
}

/** @brief Whether @p window is docked anywhere in the dock tree of @p space. */
[[nodiscard]] bool docked_in(const ImGuiWindow* window, ImGuiID space) {
    return window != nullptr && window->DockNode != nullptr
        && ImGui::DockNodeGetRootNode(window->DockNode)->ID == space;
}

/**
 * @brief What the status line says when the environment opened the store.
 *
 * Written here rather than at the call so the sentence is one thing a reader
 * can find, and phrased as something that happened, because that is what it
 * is: the notice is on the bar for a few seconds and then it is history. The
 * variable is named in it because the reader is the person who can unset it,
 * and "the environment" alone would send them looking for which variable.
 */
constexpr std::string_view environment_event =
    "The store was opened with the password from the environment "
    "(HEDGR_MASTER_PASSWORD), not by pressing Unlock.";

/**
 * @brief The size of the startup notice, in pixels.
 *
 * Wide enough to carry the store's own refusal, which names a full path and
 * two version numbers, in four or five lines rather than a dozen: a notice
 * that wrapped a path across three lines is a path the reader cannot copy.
 * The height is what that sentence takes plus the headline and the air around
 * them.
 */
constexpr float notice_width = 700.0f;
constexpr float notice_height = 200.0f;

}

namespace hedgr::ui::render {

namespace {

/** @brief The grid cards snap to, anchored at the desk's work area. */
[[nodiscard]] card_grid card_grid_for(const ImGuiViewport& viewport) noexcept {
    const ImVec2 pitch(
        hedgr::ui::theme::mini_plot_width + hedgr::ui::theme::mini_plot_grid_gap,
        hedgr::ui::theme::mini_plot_height + hedgr::ui::theme::mini_plot_grid_gap);
    const auto fit = [](float room, float pitch_along) {
        return std::max(static_cast<int>(std::floor(
                            (room + hedgr::ui::theme::mini_plot_grid_gap)
                            / pitch_along)),
            1);
    };
    return card_grid{viewport.WorkPos, pitch,
        fit(viewport.WorkSize.x, pitch.x), fit(viewport.WorkSize.y, pitch.y)};
}

}

void workspace::occupied_cards(const card_grid& grid,
    hedgr::model::state::window_handle self, const render_input& input) {
    occupied_cells_.clear();
    const auto& layout = input.windows.layout();
    const auto kinds = input.windows.kind_column();
    const auto open = input.windows.open_column();
    for (std::size_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0 || open[index] == 0
            || static_cast<hedgr::model::domain::window_kind>(kinds[index])
                != hedgr::model::domain::window_kind::mini_plot
            || index == self.index)
            continue;
        const hedgr::model::state::window_handle other{
            static_cast<std::uint32_t>(index), layout.generation[index]};
        card_key_ = "###";
        card_key_ += input.windows.id(other);
        /*
         * A card not begun yet this frame is read from its saved settings, so
         * a card opened before the rest of a restored desk is drawn still
         * misses their cells.
         */
        if (const ImGuiWindow* w = ImGui::FindWindowByName(card_key_.c_str())) {
            occupied_cells_.push_back(cell_for(grid, w->Pos));
        } else if (const ImGuiWindowSettings* saved =
                       ImGui::FindWindowSettingsByID(
                           ImHashStr(card_key_.c_str()))) {
            occupied_cells_.push_back(cell_for(grid,
                ImVec2(static_cast<float>(saved->Pos.x),
                    static_cast<float>(saved->Pos.y))));
        }
    }
}

workspace::workspace() = default;

workspace::~workspace() = default;

void workspace::load_market_view(const std::filesystem::path& path) {
    static_cast<void>(hedgr::ui::render::load_market_view(path, market_view_));
}

void workspace::save_market_view(const std::filesystem::path& path) const {
    static_cast<void>(
        hedgr::ui::render::save_market_view(path, market_view_));
}

void workspace::unlock_with(std::string_view master_password) {
    /*
     * The buffer is the prompt's own, so an unattended launch sets exactly what
     * a person would have typed and the rest of the path is unchanged: the same
     * submit, the same command, the same wipe. A second copy of the password
     * held anywhere else would be a second thing to remember to clear.
     */
    const auto length = std::min(master_password.size(),
        master_password_.size() - 1);
    std::copy_n(master_password.begin(), length, master_password_.begin());
    master_password_[length] = '\0';
    armed_password_ = true;

    /*
     * The interface is told, now and for as long as the run keeps the password
     * it was handed. An unlock nobody performed that leaves no trace is the
     * whole defect this records against: the store would look exactly as it
     * does when a person opens it, and the gate would look absent.
     */
    environment_unlock_ = true;
}

void workspace::render(const render_input& input) {
    /*
     * The control is told the state it is drawing before any window gets the
     * chance to draw it, and its answer is read after the walk. It lives on
     * the Instances window's status bar, so that window's renderer is what
     * fills it in; these are the defaults for a frame in which it does not.
     */
    lock_control_.lock = input.store_lock;
    lock_control_.reason = input.lock_message;
    lock_control_.open_requested = false;
    lock_control_.close_requested = false;

    /*
     * The background is submitted first and nothing precedes it, because it is
     * the surface the windows are read against. There is no application-level
     * toolbar to draw above it: the one toolbar each window has is the
     * browser's, beside the tree.
     */
    draw_background(input);

    /*
     * The top bar goes before the dock space and is what insets the work area
     * at the top, so the dock space below reads WorkPos and needs no knowledge
     * of it. Its answer joins the lock control's, which is read after the walk.
     */
    if (!logo_loaded_) {
        logo_loaded_ = true;
        if (const auto path = logo_svg_path(); !path.empty())
            logo_svg_ = read_text_file(path);
    }
    menu_bar_result top_bar;
    draw_menu_bar(input, logo_svg_, desk_docked_, menu_bar_, top_bar);
    if (top_bar.focus_instances)
        focus_instances_ = true;
    lock_control_.close_requested |= top_bar.lock_requested;
    lock_control_.open_requested |= top_bar.unlock_requested;
    if (top_bar.undock_all)
        next_move_ = dock_move{.all = true};
    exit_requested_ |= top_bar.quit_requested;

    draw_dock_space();

    /*
     * The runtime's latest failure, raised on the status bar before the walk
     * that draws it. Raised every frame rather than announced once, because
     * the bar is what remembers it: a failure that is still true is raised
     * again, which is idempotent by construction, and one the runtime has
     * cleared takes the bar down with it.
     *
     * This is the general rule and the startup notice below is its degenerate
     * case: every failure the runtime records reaches the bar, except the one
     * failure that leaves no bar to reach, which has to be drawn by the frame.
     */
    if (input.runtime_error.empty())
        status_.clear_error();
    else
        status_.show_error(input.runtime_error, input.now_ns);

    /*
     * The startup notice, when the store never came up. Before the walk,
     * because it reports on the state the walk is about to find: an empty
     * window table is the same fact the notice is describing, and a frame in
     * which the notice and the table disagree is not a frame this can produce.
     */
    draw_startup_failure(input);

    /*
     * The open store ends the prompt, and the frame that notices it is the
     * frame the environment's own unlock, if there was one, gets announced.
     * Here rather than at the moment the password was armed, because between
     * the two the derivation runs and the prompt is what is on screen: a
     * notice posted then would spend most of its few seconds on a frame nobody
     * was reading.
     */
    if (shows_instances(input)) {
        if (lock_form_open_)
            close_lock_prompt();
        clear_password_buffers();

        if (!environment_announced_ && environment_unlock_) {
            status_.show_event(environment_event, input.now_ns);
            environment_announced_ = true;
        }
    }

    /*
     * The password form is put in front of the operator once, on the first
     * frame that finds the store shut and was handed no password. The other way
     * to open it is the lock control on the status bar. Offered once and not
     * every frame, because Cancel and the title bar have to be able to put it
     * down and have it stay down.
     *
     * The drawing itself happens at the end of the frame, not here. It was here
     * until it was noticed that it opened underneath the Instances window: a
     * window is drawn in submission order, the walk below submits the Instances
     * window over the middle of the screen, and a prompt nobody can see is a
     * store nobody can open.
     */
    if (!lock_prompt_offered_ && !armed_password_ && !shows_instances(input)) {
        lock_prompt_offered_ = true;
        open_lock_prompt();
    }

    /*
     * An unattended launch left a password in the buffer, so it is submitted
     * as soon as the store says it wants one. The form does not have to be
     * open for that: the environment's password arrives without anybody
     * clicking the lock, and if the submission needed an open form then an
     * unattended launch would sit locked until somebody clicked -- which is
     * the same silent no-op as before, in the other direction.
     *
     * It is gated on @c armed_password_ and not merely on a non-empty buffer,
     * because the buffer is also what the operator types into while the form
     * is open. Submitting on a non-empty buffer would fire the moment anyone
     * typed their first character.
     */
    if (armed_password_ && master_password_[0] != '\0' && !input.unlocking)
        submit_master_password(input);

    const auto& layout = input.windows.layout();

    /*
     * Windows are walked in slot order and the hot columns are read directly,
     * rather than through the per-row accessors. The columns are contiguous
     * arrays, so this is a stride-1 walk: one cache line per window decides
     * whether that window costs anything at all.
     */
    const auto kind_column = input.windows.kind_column();
    const auto open_column = input.windows.open_column();
    const std::string focus_request = input.windows.take_focus_request();
    const auto refresh_column = input.windows.next_refresh_column();

    int undocked = 0;
    for (std::size_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;

        const auto kind =
            static_cast<hedgr::model::domain::window_kind>(kind_column[index]);

        /*
         * The open flag culls every window the operator has closed, the
         * Instances window included. The root used to be drawn whatever the flag
         * said, because closing it left no way to open it again. The shortcut
         * bar now asks the runtime to open it, so it follows the same rule as
         * every other window and disappears when it is closed.
         */
        if (open_column[index] == 0)
            continue;

        const hedgr::model::state::window_handle handle{
            static_cast<std::uint32_t>(index),
            layout.generation[index]};

        /*
         * The second gate. The first one above is the operator's: whether the
         * window is open at all. This one is the store's, and it admits only
         * the kinds that show something which is not read from the store. While
         * the lock is closed the loop below is unreachable anyway, so this is
         * the rule stated where a reader will find it rather than a condition
         * that happens to be true.
         */
        if (!shows_window(kind, input, input.windows.id(handle)))
            continue;

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

        /*
         * A capture names the one window it is about, and ImGui brings it to
         * the front for that frame. Windows here float over one another, and a
         * capture of a window that something else happens to be drawn over is a
         * frame showing the wrong surface without saying so. The name is a
         * window id rather than a title, because a title changes with the data
         * and an id does not.
         *
         * The raise stands aside while any popup is open. A window focused
         * from under a popup it just opened takes the popup down with it, and
         * an active child loses the focus a keyboard copy is routed to, so a
         * capture of a chooser or of a copy would find the surface gone before
         * it could be read.
         */
        if (capture_focus_id() == input.windows.id(handle)) {
            const bool any_popup = ImGui::IsPopupOpen(nullptr,
                ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            if (!any_popup)
                ImGui::SetNextWindowFocus();
        }

        /*
         * A shortcut to the Instances window asks for it on top, and the
         * request was left by the bar on the previous frame. It is consumed
         * here, at the only point in the frame where the window can still be
         * raised, rather than where the click was read.
         */
        if (!focus_request.empty()
            && focus_request == input.windows.id(handle)) {
            ImGui::SetNextWindowFocus();
        }
        if (kind == hedgr::model::domain::window_kind::instance_manager
            && focus_instances_) {
            ImGui::SetNextWindowFocus();
            focus_instances_ = false;
        }

        /*
         * The visible title can change with the data, so the ImGui id
         * is pinned to the stable id after the ### marker. Without it a
         * retitled window would be a different window to ImGui, and would
         * lose its position, size and collapsed state.
         */
        begin_label_.clear();
        begin_label_ += hedgr::ui::theme::window_kind_icon(kind);
        begin_label_ += "  ";
        const auto title_start = begin_label_.size();
        append_window_title(begin_label_, kind, input.windows.state(handle),
            input);
        const auto title_size = begin_label_.size() - title_start;
        begin_label_ += "###";
        begin_label_ += input.windows.id(handle);

        const bool card = kind == hedgr::model::domain::window_kind::mini_plot;
        if (!card)
            ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 220.0f),
                ImVec2(FLT_MAX, FLT_MAX));

        /*
         * The size a window opens at, once. The browser surfaces are the reason
         * it is this wide: a folder tree and a list of columns need room, and a
         * window that opened at ImGui's default would show a browser squeezed
         * to the point where its own controls are the only thing visible. It is
         * applied while the window is young, so a size an operator has chosen
         * is never overridden on a later frame.
         */
        const ImVec2 default_size =
            kind == hedgr::model::domain::window_kind::accounts
            ? ImVec2(1180.0f, 820.0f)
            : kind == hedgr::model::domain::window_kind::command_centre
            ? ImVec2(1120.0f, 760.0f)
            : kind == hedgr::model::domain::window_kind::chart
            ? ImVec2(1000.0f, 620.0f)
            : kind == hedgr::model::domain::window_kind::crm_matrix
            ? ImVec2(1180.0f, 760.0f)
            : ImVec2(700.0f, 460.0f);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);

        if (move_ && move_->all) {
            /*
             * Undock all: every window docked in the desk floats, one title bar
             * lower than the last so the stack can be told apart. A window
             * that is already floating is not named by this move.
             */
            if (docked_in(ImGui::FindWindowByName(begin_label_.c_str()),
                    dock_space_)) {
                const ImGuiViewport& viewport = *ImGui::GetMainViewport();
                const auto known = ui_state_.find(
                    hedgr::model::state::key_parts<1>{input.windows.id(handle)});
                const ImVec2 size = floating_size_for(
                    known != ui_state_.end() ? known->second.floating_size
                                             : ImVec2(),
                    default_size, viewport);
                const float step = ImGui::GetFrameHeight();
                const float room = std::min(viewport.WorkSize.x - size.x,
                    viewport.WorkSize.y - size.y);
                const float lap = std::floor(std::max(room, 0.0f) / step) + 1.0f;
                const float offset =
                    std::fmod(static_cast<float>(undocked++), lap) * step;
                ImGui::SetNextWindowDockID(0);
                ImGui::SetNextWindowPos(popped_position(
                    ImVec2(viewport.WorkPos.x + offset,
                        viewport.WorkPos.y + offset), size, viewport));
                ImGui::SetNextWindowSize(size);
            }
        } else if (move_ && move_->window == handle) {
            if (move_->into_desk) {
                ImGui::SetNextWindowDockID(move_->to);
            } else {
                ImGui::SetNextWindowDockID(0);
                ImGui::SetNextWindowPos(move_->pos);
                ImGui::SetNextWindowSize(move_->size);
            }
        }

        /*
         * Every window carries a close control, the root included. Closing is a
         * command rather than a local hide: the runtime owns the window table,
         * so a window hidden here would come back on the next frame with its own
         * row still saying it was open, and the two would disagree about a
         * window nobody could see.
         *
         * The root is closable only while the store is open. The shortcut bar is
         * what opens it again and the bar is drawn from the catalogue, so a root
         * closed while the store is locked could not be brought back, because
         * the lock control lives on the root and there would be nothing left to
         * click.
         */
        const bool closable =
            kind != hedgr::model::domain::window_kind::instance_manager
            || shows_instances(input);
        bool keep_open = true;
        /*
         * Only the command centre carries a menu bar, and ImGui refuses to draw
         * one unless the window was created with the flag. It is set here
         * because this loop owns the Begin call and a renderer only fills the
         * body.
         */
        ImGuiWindowFlags window_flags =
            kind == hedgr::model::domain::window_kind::command_centre
                || kind == hedgr::model::domain::window_kind::workspace
            ? ImGuiWindowFlags_MenuBar
            : ImGuiWindowFlags_None;
        /*
         * A card is its own tile, so its window has no padding, border or
         * background to show round it, and no title bar either. The body is the
         * handle: a drag on it moves the card, and the close control and the
         * context menu close it. Pushed only around Begin because the window
         * reads these when it is begun.
         *
         * A card never docks and never resizes. Dock nodes share their space,
         * so a tile split off another shrinks both and docking cannot hold a
         * fixed size. A card floats at one size and snaps to a grid instead.
         * NoDocking also undocks a card whose saved settings say it was docked.
         */
        if (card) {
            window_flags |= ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoBackground
                | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::SetNextWindowSize(ImVec2(hedgr::ui::theme::mini_plot_width,
                hedgr::ui::theme::mini_plot_height));
            if (const ImGuiViewport& viewport = *ImGui::GetMainViewport();
                !ImGui::FindWindowByName(begin_label_.c_str())
                && ImGui::FindWindowSettingsByID(
                       ImHashStr(begin_label_.c_str())) == nullptr) {
                /* First open: the first cell no other card holds. */
                const auto grid = card_grid_for(viewport);
                occupied_cards(grid, handle, input);
                ImGui::SetNextWindowPos(cell_position(grid,
                    first_free_cell(grid, occupied_cells_)));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        }
        /*
         * A window docked in the desk draws no fill of its own: ImGui rounds
         * it only where it meets the dock space's edge, so the desk paints
         * its card (paint_dock_cards). Nor a scrollbar track, which ImGui
         * squares at the same corners and would show over the card's. Read
         * from last frame, as a window is not docked until it has been begun.
         */
        const bool bare = !card
            && docked_in(ImGui::FindWindowByName(begin_label_.c_str()),
                dock_space_);
        if (bare) {
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,
                ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        }
        const bool begun = ImGui::Begin(begin_label_.c_str(),
            closable ? &keep_open : nullptr, window_flags);
        if (bare)
            ImGui::PopStyleColor();
        if (card)
            ImGui::PopStyleVar(3);
        if (!begun) {
            /*
             * Collapsed or fully clipped: ImGui still wants its End, and
             * nothing inside the window is submitted. This is the content
             * culling that keeps a workspace of hundreds of windows cheap.
             */
            ImGui::End();
            continue;
        }
        if (!card)
            hedgr::ui::theme::shade_window_background(
                hedgr::ui::theme::window_gradient);

        const hedgr::model::state::key_parts<1> key{input.windows.id(handle)};
        auto entry = ui_state_.find(key);
        if (entry == ui_state_.end()) {
            entry = ui_state_.try_emplace(
                hedgr::model::state::composite_key<1>{key}).first;
            /*
             * A window that has not been seen before opens on the choices the
             * last window left, while the windows already open keep their own.
             */
            entry->second.market_view = market_view_;
        }
        window_ui_state& state = entry->second;
        if (!ImGui::IsWindowDocked())
            state.floating_size = ImGui::GetWindowSize();
        if (const auto draw = renderer_for(kind))
            draw(handle, input, state, status_, lock_control_);

        /*
         * A card has no title bar, so no close button. Its close control shows
         * on hover, and the context menu closes it too.
         */
        if (card) {
            ImGuiWindow* const window = ImGui::GetCurrentWindow();
            const bool moving = ImGui::GetCurrentContext()->MovingWindow
                == window;
            if (state.was_moving && !moving) {
                /* The drag ended: land on the grid, off any other card. */
                const auto grid = card_grid_for(*ImGui::GetMainViewport());
                occupied_cards(grid, handle, input);
                ImGui::SetWindowPos(window,
                    snap_position(grid, window->Pos, occupied_cells_),
                    ImGuiCond_Always);
            }
            state.was_moving = moving;
        }
        if (card
            && ImGui::BeginPopupContextWindow("##card-menu",
                ImGuiPopupFlags_MouseButtonRight)) {
            if (ImGui::MenuItem("Close"))
                keep_open = false;
            ImGui::EndPopup();
        }

        switch (std::exchange(state.tile, tile_request::none)) {
        case tile_request::none:
            break;
        case tile_request::dock:
            next_move_ = dock_move{.window = handle, .into_desk = true};
            break;
        case tile_request::close:
            keep_open = false;
            break;
        }

        if (!keep_open) {
            hedgr::model::command::command request;
            request.kind = hedgr::model::command::command_kind::close_window;
            request.window_id = std::string{input.windows.id(handle)};
            input.commands.push(std::move(request));

            /*
             * The close is announced here, where the operator's click becomes a
             * command, rather than where the runtime later removes the row. The
             * sound belongs to the action, and the runtime's bookkeeping is not
             * a thing the operator did. See ADR 8 for why this is a name on the
             * bus rather than a call to the player.
             */
            hedgr::model::signal::signal closed;
            closed.name = std::string(hedgr::model::signal::names::window_closed);
            closed.detail = std::format("{} closed",
                std::string_view{begin_label_}.substr(title_start, title_size));
            closed.at_ns = input.now_ns;
            hedgr::model::signal::bus().raise(std::move(closed));
        }

        ImGui::End();
    }

    /*
     * The lock control's answer, read after the walk that drew it. One control
     * with two jobs: an open store is closed, anything else asks for the
     * password. Nothing else in the interface opens the form or locks the
     * store, which is what makes this the lock control rather than one of two.
     */
    if (lock_control_.close_requested) {
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::lock_store;
        input.commands.push(std::move(request));
        close_lock_prompt();
        environment_unlock_ = false;
    } else if (lock_control_.open_requested) {
        lock_prompt_offered_ = true;
        open_lock_prompt();
    }

    /*
     * Last, and after the control that may have just asked for it, so the form
     * is the final window submitted and therefore the one on top of everything
     * the walk drew. See the note where it is opened.
     */
    draw_lock_prompt(input);

    /* A move outlives one frame at most, so one whose window went is dropped. */
    move_ = std::exchange(next_move_, std::nullopt);

    /*
     * Read once per frame, after every window has docked or left, so the menu
     * and the dock space agree on whether Undock all is live. A frame behind
     * the click that docks the first window, which nobody can see.
     */
    desk_docked_ = holds_windows(ImGui::DockBuilderGetNode(dock_space_));
}

void workspace::draw_dock_space() {
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0)
        return;

    /*
     * ImGui's DockSpaceOverViewport, written out for the flags it needs. The
     * space fills the work area, which the top bar has already inset, so a
     * tile docked against the bottom edge reaches the edge of the viewport.
     *
     * The centre passes clicks through while nothing is docked there, so the
     * desk behind it, and every window floating over it, are as reachable as
     * they were before there was a dock space.
     */
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("###hedgr-desk", nullptr, flags);
    ImGui::PopStyleVar(3);

    dock_space_ = ImGui::GetID("desk");
    if (move_ && move_->into_desk)
        move_->to = dock_target(dock_space_);
    /*
     * The dock nodes' tab bars are submitted inside this call and take the
     * font in force, which is how their labels get the reference's monospace
     * without a hook per tab. Windows docked here draw their bodies later, in
     * their own Begin, so their text keeps the base face. The tab strip's
     * padding, spacing, label colour and backing are pushed the same way, so
     * the reference's 28 pixel tabs do not reshape any other frame.
     */
    ImGui::PushFont(icon::mono_font());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(hedgr::ui::theme::tab_padding_x,
            hedgr::ui::theme::tab_padding_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
        ImVec2(hedgr::ui::theme::tab_spacing,
            ImGui::GetStyle().ItemInnerSpacing.y));
    ImGui::PushStyleColor(ImGuiCol_Text, hedgr::ui::theme::tab_label_colour());
    /*
     * Clear, because ImGui fills the strip behind the tabs, the whole space
     * (the Passthru flag) and the splitter gaps in these colours with square
     * corners, and rules each gap in the border colour. paint_dock_cards
     * paints the strip and the body rounded instead. A splitter still shows
     * under the pointer, in the resize grip colours.
     */
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::DockSpace(dock_space_, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::PopStyleColor(2);
    hedgr::ui::theme::paint_dock_cards(dock_space_);
    /* ImGui paints each tab flat; the fade is laid over its vertices here. */
    hedgr::ui::theme::shade_dock_tabs();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::End();
}

bool workspace::shows_lock_prompt(const render_input& input) const noexcept {
    /*
     * Never while the store is open: there is nothing to ask for, and the
     * frame that notices it closes the form. Otherwise it is on screen exactly
     * when it has been opened, which is when the lock control was clicked.
     *
     * The form stays up while a derivation is running, and that is the point
     * rather than an oversight: the key derivation is slow on purpose, and a
     * form that disappears for half a second leaves an operator looking at a
     * background wondering whether the application has hung. It shows that it
     * is working instead.
     */
    if (!lock_form_open_)
        return false;
    return input.store_lock != hedgr::model::domain::store_lock::unlocked;
}

void workspace::open_lock_prompt() {
    /*
     * Opening the form is the operator asking to be let in, so whatever they
     * typed and abandoned last time is gone and the fields start empty. A
     * password carried over from an earlier attempt would be a field already
     * filled in with something nobody can see.
     */
    clear_password_buffers();
    armed_password_ = false;
    master_password_visible_ = false;
    create_mismatch_ = false;
    lock_form_open_ = true;
    prompt_open_ = true;
}

void workspace::close_lock_prompt() {
    clear_password_buffers();
    armed_password_ = false;
    master_password_visible_ = false;
    create_mismatch_ = false;
    lock_form_open_ = false;
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
    request.password =
        hedgr::model::domain::secret_string{
            std::string{master_password_.data()}};
    input.commands.push(std::move(request));

    /*
     * The buffers are wiped the moment the command holds the password, so the
     * typed copy lives for one frame and the command's copy is the runtime's to
     * clear. The eye toggle is reset with them: a reveal that survived the
     * password it revealed would be showing the next one.
     */
    clear_password_buffers();
    armed_password_ = false;
    master_password_visible_ = false;
    create_mismatch_ = false;
}

void workspace::draw_startup_failure(const render_input& input) {
    const auto notice = startup_notice_for(input.startup_failure);
    if (notice.empty())
        return;

    /*
     * Drawn by the frame and not as a window row, because this is the failure
     * that leaves no rows to draw: the store could not be opened, so the
     * window table is empty and the walk below has nothing to visit. The
     * notice has to come from the frame itself.
     *
     * It is a window and not a modal, like every other surface here. The
     * application behind it can still be looked at and closed, and the title
     * bar puts the notice down for an operator who has read it and wants the
     * rest of the screen.
     */
    const ImVec2 size(notice_width, notice_height);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    /*
     * Centred horizontally and near the top, the first time. Not centred
     * vertically, because the eye lands at the top of a screen and a notice
     * there reads before the background does.
     */
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) / 2.0f,
            viewport->Pos.y + 80.0f),
        ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoDocking;

    /*
     * The close button is the way out, and it is offered because the
     * application behind the notice still works and an operator may want to
     * look at it. ImGui clears the flag on the click, and that clear is the
     * dismissal: the notice is not submitted again for the rest of the run.
     */
    if (!startup_notice_open_)
        return;
    const char title[] = "The store could not be opened###hedgr-startup-failure";
    if (!ImGui::Begin(title, &startup_notice_open_, flags)) {
        ImGui::End();
        return;
    }
    hedgr::ui::theme::shade_window_background(
        hedgr::ui::theme::window_gradient);

    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + notice_width - 40.0f);

    /*
     * The same shape every other notice in this interface has: a red glyph,
     * the headline beside it, then the message. A failure read here has to
     * look like a failure read in a notice window, and the glyph is what makes
     * it look like one at a glance rather than after reading the sentence.
     *
     * The reason below is the runtime's own sentence, unmodified. It names the
     * file, both schema versions where that is the fault, and the remedy, and
     * a second wording here would be a second diagnosis to keep in step.
     */
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(hedgr::ui::theme::failure_colour));
    ImGui::TextUnformatted(hedgr::ui::icon::error);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(hedgr::ui::theme::failure_colour),
        "%s", notice.heading.c_str());

    ImGui::Separator();
    ImGui::TextUnformatted(notice.reason.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
}

void workspace::draw_lock_prompt(const render_input& input) {
    using hedgr::model::domain::store_lock;

    if (!shows_lock_prompt(input))
        return;

    const bool creating = input.store_lock == store_lock::absent;

    /*
     * Fixed, and sized to what is in it. The create form is taller because it
     * asks twice, explains what the password is for and carries the rule list
     * the deployment stated; the unlock form asks once and says one line,
     * because there is nothing to assess about a password that already exists.
     * The sentence below is wrapped rather than run off the edge, because a
     * window this width cannot carry it on one line and a clipped explanation
     * is worse than none.
     *
     * Fixed width, no resize and no collapse, for the same reason every other
     * form here has them: a password box that can be resized invites a size
     * that hides the eye toggle. Not fixed in position, because the title bar
     * is how the prompt is put down.
     */
    const ImVec2 size(460.0f, creating ? 400.0f : 150.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    /*
     * Centred the first time, and left wherever the operator puts it after
     * that. The prompt is the only surface on a first run, so it opens where
     * the eye already is; once it has been moved, moving it back would be the
     * interface arguing with the person who moved it.
     */
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) / 2.0f,
            viewport->Pos.y + (viewport->Size.y - size.y) / 2.0f),
        ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoDocking;

    /*
     * A window and not a modal. A trading system has several things happening
     * at once, and a modal stops all of them for a question that concerns one
     * of them. The prompt sits beside the windows the operator is already
     * looking at, and putting it down -- with the title bar's close button or
     * with Cancel -- leaves everything else exactly as it was.
     */
    /*
     * The title bar's close button is the second way out, beside Cancel. The
     * flag is handed to ImGui so that button exists, and it is re-armed every
     * time the window appears, because ImGui clears it when it is clicked and
     * that clear is the click.
     */
    const char* title = creating ? "Create the master password###hedgr-lock"
                                 : "Unlock the store###hedgr-lock";
    if (!ImGui::Begin(title, &prompt_open_, flags)) {
        ImGui::End();
        return;
    }
    hedgr::ui::theme::shade_window_background(
        hedgr::ui::theme::window_gradient);

    if (creating) {
        ImGui::TextWrapped(
            "The whole local store sits behind this password. It cannot be "
            "recovered, and losing it loses the instances and folders as well "
            "as the stored credentials.");
    }

    /*
     * The unlock form says what it is for and nothing about the store's state.
     * The state has one home, which is the status bar's own sentence beside the
     * lock icon: an owner of two places would show "the store is locked" on
     * this form and again in the bar underneath it, and the operator would read
     * two facts where there is one. The fields below and the title say what to
     * do here; the bar says what the store is.
     */

    ImGui::Spacing();

    /*
     * The reveal toggle sits immediately right of the password box, because
     * that is where an eye is looked for. Same construction as the connect
     * form's, so the two read as one control used twice rather than two
     * controls that happen to look alike.
     */
    const float eye = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-(eye + ImGui::GetStyle().ItemInnerSpacing.x));
    bool submitted_by_enter = ImGui::InputTextWithHint("##master-password",
        "master password", master_password_.data(), master_password_.size(),
        (master_password_visible_ ? 0 : ImGuiInputTextFlags_Password)
            | ImGuiInputTextFlags_EnterReturnsTrue);
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
        submitted_by_enter |=
            ImGui::InputTextWithHint("##master-password-again",
                "master password again", master_password_again_.data(),
                master_password_again_.size(),
                (master_password_visible_ ? 0 : ImGuiInputTextFlags_Password)
                    | ImGuiInputTextFlags_EnterReturnsTrue);

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
     * than after the buttons: it is about this field, and it belongs next to
     * it.
     */
    if (!input.lock_message.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                               hedgr::ui::theme::store_lock_colour(store_lock::failed)),
            "%s", input.lock_message.c_str());
    }

    /*
     * The rules and the strength, on the create prompt and nowhere else. The
     * unlock prompt has a password that already exists, so there is nothing
     * there to assess: the only question is whether the deployment accepts it,
     * and only the deployment can answer that.
     */
    if (creating) {
        ImGui::Spacing();
        hedgr::ui::widget::draw_password_meter(input.password_policy,
            std::string_view{master_password_.data()});
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
            close_lock_prompt();
        }

        /*
         * Enter submits, which is what every other password field in every
         * other program does, and the button is the only other way in. Both
         * paths run the same submit, so neither can drift from the other; and
         * Enter is gated on @c complete exactly as the button is, because a
         * keypress must not be a way past the confirmation the form asks for.
         */
        if (submitted_by_enter && complete)
            submit_master_password(input);
        else if (confirmed && complete)
            submit_master_password(input);
    }

    /*
     * The title bar's close button puts the prompt down the same way Cancel
     * does, and it is the second of the two ways out. A window that could be
     * closed with the mouse but insisted on an answer would be a window that
     * cannot be dismissed, which is the thing being removed here.
     *
     * Read after the body so a click that both typed and closed is not read as
     * a dismissal before the typing happened.
     */
    if (ImGui::IsWindowAppearing())
        prompt_open_ = true;
    if (!prompt_open_)
        close_lock_prompt();

    ImGui::End();
}

}