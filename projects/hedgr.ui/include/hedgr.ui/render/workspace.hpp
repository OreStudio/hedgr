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
#ifndef HEDGR_UI_RENDER_WORKSPACE_HPP
#define HEDGR_UI_RENDER_WORKSPACE_HPP

#include "hedgr.model/state/composite_key.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/menu_bar.hpp"
#include "hedgr.ui/render/mini_plot.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/shortcut_bar.hpp"
#include "hedgr.ui/widget/menu_bar.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hedgr::ui::render {

/**
 * @brief The frame: every surface Hedgr draws, in one call.
 *
 * The workspace owns the interface's scratch state and nothing else. All
 * durable state lives in the model's tables, and all change reaches them
 * through the command queue, so the workspace can be destroyed and rebuilt
 * without losing anything a person would miss.
 *
 * The top bar holds the logo and the system menu behind it. Actions stay icon
 * buttons with tooltips, because a pull-down menu hides what can be done
 * behind a click that does not name it.
 */
class HEDGR_UI_EXPORT workspace final {
public:
    /** @brief Starts with no scratch state, which is exactly a fresh frame. */
    workspace();

    /** @brief Out of line so callers need no ImGui headers to hold one. */
    ~workspace();

    workspace(const workspace&) = delete;
    workspace& operator=(const workspace&) = delete;

    /**
     * @brief Arms the lock prompt with a password to submit on the next frame.
     *
     * For an unattended launch, where there is nobody to type one. The password
     * is held in the same buffer the prompt edits and is wiped the moment it is
     * submitted, so it exists in this object for exactly as long as a typed one
     * would. Calling this on a store that is already unlocked does nothing.
     *
     * The store is then opened without anybody pressing anything, so the
     * interface says so for as long as that is true: see
     * @c environment_unlock_ below, whose notice lands on the Instances
     * window's status bar. An unlock that looks exactly like a person's
     * is the failure this records against, because it makes the gate look as
     * though it is not there.
     */
    void unlock_with(std::string_view master_password);

    /**
     * @brief Draws every open window, gated on the lock.
     *
     * Must be called between ImGui::NewFrame and ImGui::Render. Pushes at most
     * one command per user action into input.commands; it never touches the
     * tables it reads.
     *
     * There is no application-level toolbar to draw first. The one toolbar
     * each window has is the browser's, beside the tree, and the actions the
     * application's strip used to carry are on the surfaces that own them:
     * probe-all on the Instances window, the lock control on the status bar.
     */
    void render(const render_input& input);

    /**
     * @brief The market-data choices every such window reads and writes.
     *
     * The frame owns them because they outlive the window that made them. The
     * application hands this reference to the frame's render input so a chart
     * or curve window can change the choice in place.
     */
    [[nodiscard]] market_view_state& market_view() noexcept {
        return market_view_;
    }

    /** @brief Reads the remembered choices, leaving them on failure. */
    void load_market_view(const std::filesystem::path& path);

    /** @brief Writes the remembered choices to @p path. */
    void save_market_view(const std::filesystem::path& path) const;

    /** @brief The desk's dock space, zero until a frame with docking on. */
    [[nodiscard]] ImGuiID dock_space() const noexcept { return dock_space_; }

private:
    /**
     * @brief A window to dock into the desk or float out of it.
     *
     * Asked for in one frame and done before the window's Begin in the next,
     * because a window's dock can only be set before it is begun.
     */
    struct dock_move final {
        hedgr::model::state::window_handle window;
        /** @brief Every window docked in the desk, out to its floating size. */
        bool all = false;
        /** @brief Into the desk's dock space, or out to @c pos and @c size. */
        bool into_desk = false;
        /** @brief The node it docks into, chosen before the dock space draws. */
        ImGuiID to = 0;
        ImVec2 pos{};
        ImVec2 size{};
    };

    /**
     * @brief Fills @c occupied_cells_ with the cell of every open card but
     * @p self.
     *
     * Called only when a card opens or a drag ends, so a steady frame does not
     * touch it and the vector's capacity is kept between calls.
     */
    void occupied_cards(const card_grid& grid,
        hedgr::model::state::window_handle self, const render_input& input);

    /** @brief Draws the dock space that fills the desk above the shortcut bar. */
    void draw_dock_space(const render_input& input);

    /**
     * @brief Draws the notice that says the store could not be opened.
     *
     * The only surface this class draws that does not come from a window row,
     * because it is the report of the failure that leaves the window table
     * empty. Nothing at all is drawn when the store came up, so a healthy run
     * is unchanged by it.
     */
    void draw_startup_failure(const render_input& input);

    /**
     * @brief Draws the password form, when the operator has opened it.
     *
     * Two modes, chosen by the state and not by the caller: @c absent asks for
     * the password twice because it is being created, and @c locked or
     * @c failed ask for it once because it is being checked.
     */
    void draw_lock_prompt(const render_input& input);

    /** @brief Whether the form is on screen this frame. */
    [[nodiscard]] bool shows_lock_prompt(const render_input& input) const noexcept;

    /**
     * @brief Opens the form and empties its fields.
     *
     * Called by the only thing that opens it, which is the lock control on the
     * status bar. Whatever was typed and abandoned last time is cleared, so a
     * second attempt never starts with a field nobody can see the contents of.
     */
    void open_lock_prompt();

    /** @brief Puts the form down without submitting anything. */
    void close_lock_prompt();

    /** @brief Pushes a set_master_password or an unlock_store command. */
    void submit_master_password(const render_input& input);

    /** @brief Wipes both typed buffers. */
    void clear_password_buffers() noexcept;

    /**
     * @brief Scratch state per window, keyed by the window's stable id.
     *
     * Keyed by stable id rather than by handle so that a window which is
     * closed and reopened, or which survives a rebuild of the window table,
     * keeps whatever the person had typed into it.
     */
    hedgr::model::state::composite_map<1, window_ui_state> ui_state_;

    /**
     * @brief The ImGui label of the window being drawn, reused by every window.
     *
     * Built for each window before its culling check, so a buffer that keeps
     * its capacity is what lets a culled window cost no allocation.
     */
    std::string begin_label_;

    /** @brief The choices the market-data screens share across windows. */
    market_view_state market_view_;

    std::vector<card_cell> occupied_cells_;
    std::string card_key_;

    ImGuiID dock_space_ = 0;

    /** @brief Whether any window was docked in the desk at the last frame's end. */
    bool desk_docked_ = false;

    /** @brief The move asked for this frame, and the one to do this frame. */
    std::optional<dock_move> next_move_;
    std::optional<dock_move> move_;

    /*
     * The master password prompt's own scratch state.
     *
     * Fixed-size buffers rather than std::string because they are handed to
     * ImGui::InputText, which edits a caller-owned buffer in place. They are
     * wiped with std::fill as soon as the password is submitted, so the
     * password exists in this object only while it is being typed; the copy
     * that reaches the runtime travels in a command, which the runtime clears
     * as soon as it has used it.
     */
    std::array<char, 128> master_password_{};
    std::array<char, 128> master_password_again_{};
    bool master_password_visible_ = false;
    bool create_mismatch_ = false;

    /**
     * @brief Whether the password form is open.
     *
     * The whole of the form's visibility, because nothing else opens it. The
     * store being locked does not open it -- the operator clicks the lock for
     * that -- so a launch that nobody touches draws the application with its
     * content obscured and nothing in the way.
     */
    bool lock_form_open_ = false;

    /**
     * @brief Whether the password in the buffer came from the environment.
     *
     * The buffer is shared between the two ways a password arrives: typed into
     * the form, and handed over by @c unlock_with. Only the second may be
     * submitted on the operator's behalf, so the two are told apart rather
     * than inferred from a non-empty buffer -- which would fire the moment
     * somebody typed their first character.
     */
    bool armed_password_ = false;

    /**
     * @brief Whether the prompt's window is open, handed to ImGui.
     *
     * ImGui draws a close button on the title bar only when it is given
     * somewhere to report the click, and this is that somewhere: it clears the
     * flag when the button is pressed. The flag is re-armed every time the
     * window appears, and a cleared flag is read once as a dismissal -- which
     * is what makes the title bar a way out beside Cancel.
     */
    bool prompt_open_ = true;

    /**
     * @brief Whether this run's store was opened with the environment's
     * password rather than by somebody opening it.
     *
     * Set when @c unlock_with arms the prompt and cleared when the operator
     * locks the store, which is the first moment the run's state is theirs
     * again. It changes nothing about how the store works: it exists so that
     * an unattended unlock cannot be mistaken for a person's, because a store
     * that opens itself and says nothing makes the gate look absent. The
     * Instances window's status bar carries a note while this is true.
     */
    bool environment_unlock_ = false;

    /**
     * @brief Whether the environment's own notice has been put on the bar.
     *
     * Once per run, because the notice describes the moment the store opened
     * and not the state it is in: a bar that announced it again every time the
     * operator came back to the window would be reporting history as news.
     */
    bool environment_announced_ = false;

    /**
     * @brief Whether this run has already offered the password form.
     *
     * The form is put in front of the operator once, on the first frame that
     * finds the store shut with no password handed to it. Once and not every
     * frame, because Cancel and the title bar have to be able to put the prompt
     * down and have it stay down: a form that reopened itself would be a form
     * with no way out.
     */
    bool lock_prompt_offered_ = false;

    /**
     * @brief Whether the startup notice's window is still up.
     *
     * Cleared by the title bar's close button, after which the notice stays
     * down for the rest of the run. The notice is not a modal, so putting it down is allowed: the
     * application behind it still runs, and hiding the report of the failure
     * does not undo the failure.
     */
    bool startup_notice_open_ = true;

    /**
     * @brief Whether the shortcuts bar asked for the Instances window on top.
     *
     * Bringing a window to the front has to happen before that window is
     * submitted, and the bar is drawn after every window so that it stays above
     * them. The request is therefore carried from one frame to the next rather
     * than acted on where it is made, which costs a frame nobody can see and
     * keeps the bar the topmost surface it should be.
     */
    bool focus_instances_ = false;

    /**
     * @brief The one-line notices the windows show and the frame ages.
     *
     * Interface scratch, owned here rather than by the window that draws it,
     * because the fact being announced is about the run and not about a
     * window, and because the bar outlives any one window being open. See
     * @c hedgr::ui::widget::status_bar for what it carries and for what
     * happens when two things want to say something at once.
     */
    hedgr::ui::widget::status_bar status_;

    /**
     * @brief What the lock control asked for this frame, and what it drew.
     *
     * Filled in before the windows are walked and read after it, because the
     * control is drawn by the Instances window's renderer and acted on here:
     * the renderer is a plain function pointer and cannot carry an answer
     * across the boundary. It is scratch, like everything else in this class.
     */
    lock_control lock_control_;

    /**
     * @brief The shortcuts bar's own memory of the last hovered entry.
     *
     * The bar is drawn by the frame, so its cross-frame state lives here with
     * the rest of the frame's scratch. A tick announces a change, and a change
     * needs the previous frame's answer to know there was one.
     */
    shortcut_bar_state shortcut_bar_;

    /** @brief Which top-level menu of the top bar is open. */
    hedgr::ui::widget::menu_bar_state menu_bar_;

    /**
     * @brief The logo's SVG document, read from disk on the first frame.
     *
     * Kept as text so the texture source can be asked for it every frame, as
     * that is how a texture is marked used, without a read of the file.
     */
    std::string logo_svg_;

    /** @brief Whether the logo file has been looked for, found or not. */
    bool logo_loaded_ = false;

    /*
     * There is deliberately no "I have submitted a password" flag here. The
     * runtime's @c unlocking already says whether a derivation is running, and
     * a local copy of that would be a second source of truth: it would survive
     * the derivation and leave the prompt stuck on "working" after a refusal,
     * with no way back to the field. The one frame between the click and the
     * runtime picking the command up looks after itself, because the buffers
     * are wiped on submit and an empty password leaves the button disabled.
     */
};

}

#endif
