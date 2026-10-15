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
#ifndef HEDGR_UI_RENDER_RENDERER_HPP
#define HEDGR_UI_RENDER_RENDERER_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/export.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace hedgr::ui::render {

/**
 * @brief The interface's own scratch state for one window.
 *
 * Not domain state: none of it is persisted and none of it is read by the
 * runtime. It is what a person has typed but not yet submitted, plus the
 * viewport a table is currently scrolled and filtered to.
 *
 * The buffers are fixed-size arrays rather than std::string because they are
 * handed straight to ImGui::InputText, which edits a caller-owned buffer in
 * place and needs a bound to write within.
 */
struct window_ui_state final {
    /** @brief Filter text, as typed. */
    std::array<char, 128> filter{};

    /** @brief Whether the detail panel is expanded. */
    bool show_detail = false;

    /** @brief The selected row of the window's primary table, or -1. */
    int selected_row = -1;

    /** @brief Stable id of a row the user asked to remove, awaiting confirm. */
    std::string pending_remove_id;

    /** @brief Principal of a connection being typed, awaiting submit. */
    std::string draft_principal;

    /** @brief Password of a connection being typed, awaiting submit. */
    std::array<char, 128> draft_password{};

    /** @brief Whether the password is drawn in the clear. */
    bool password_visible = false;
};

/**
 * @brief Everything a renderer is allowed to read, and the one thing it may do.
 *
 * The tables are const because the interface never mutates state: a renderer
 * reacts to a click by pushing a command onto the queue and the runtime applies
 * it. That is what keeps rendering free of side effects and the state tables
 * owned by a single writer.
 */
struct render_input final {
    /** @brief The instances known to this process. */
    const hedgr::model::state::instance_table& instances;

    /** @brief The accounts known to this process. */
    const hedgr::model::state::account_table& accounts;

    /** @brief The live sessions. */
    const hedgr::model::state::session_table& sessions;

    /** @brief The live windows, including the one being drawn. */
    const hedgr::model::state::window_table& windows;

    /** @brief Where a renderer puts work for the runtime. */
    hedgr::model::command::command_queue& commands;

    /** @brief The frame's monotonic clock, in nanoseconds. */
    std::int64_t now_ns = 0;

    /**
     * @brief The photographic background, as a backend texture id.
     *
     * Zero means there is none to draw. It travels as a plain integer so this
     * header does not have to include ImGui: the drawing code, which does
     * include it, casts it to the texture id type the backend expects.
     */
    std::uint64_t background_texture = 0;
};

/**
 * @brief Draws one window's contents inside an already-begun ImGui window.
 *
 * The caller owns the ImGui::Begin and ImGui::End pair, the window title and
 * its size constraints, so a renderer is only ever responsible for what goes
 * in the body. That is what lets the culling in workspace::render skip a
 * window's body entirely without the renderer knowing it happened.
 */
using renderer_fn = void (*)(hedgr::model::state::window_handle,
                             const render_input&,
                             window_ui_state&);

/**
 * @brief The renderer registered for a window kind.
 *
 * Never null: every kind in the enum has an entry in the table, and the table
 * is checked at compile time.
 */
[[nodiscard]] HEDGR_UI_EXPORT renderer_fn
renderer_for(hedgr::model::domain::window_kind kind) noexcept;

}

#endif
