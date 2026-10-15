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
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/probe_table.hpp"
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

    /** @brief The folders of every tree, including each tree's root. */
    const hedgr::model::state::folder_table& folders;

    /** @brief The labels an operator has declared. */
    const hedgr::model::state::label_table& labels;

    /** @brief The label attachments. */
    const hedgr::model::state::labelling_table& labellings;

    /** @brief The live sessions. */
    const hedgr::model::state::session_table& sessions;

    /**
     * @brief The probes' last results and next deadlines.
     *
     * Read-only like every other table here, and read for the two indicators
     * beside each instance's name: what a probe found, and -- through the
     * tooltip it carries -- why.
     */
    const hedgr::model::state::probe_table& probes;

    /** @brief The live windows, including the one being drawn. */
    const hedgr::model::state::window_table& windows;

    /** @brief Where a renderer puts work for the runtime. */
    hedgr::model::command::command_queue& commands;

    /**
     * @brief Where the master password stands.
     *
     * The whole of the local store sits behind it, so this gates the catalogue
     * and not only the credentials: while it is not @c unlocked there is
     * nothing to draw but the prompt that asks for it. See @c shows_instances.
     */
    hedgr::model::domain::store_lock store_lock =
        hedgr::model::domain::store_lock::absent;

    /**
     * @brief Whether a derivation is running on the runtime's pool right now.
     *
     * The key derivation is deliberately slow, so a prompt that shows nothing
     * for half a second reads as a hung application. This is what lets it say
     * it is working.
     */
    bool unlocking = false;

    /**
     * @brief Why the last lock attempt was refused, or empty.
     *
     * Shown beside the prompt rather than in a failure banner: a mistyped
     * password is not a store failure, and an operator who is told the store
     * broke will look in the wrong place.
     */
    std::string lock_message;

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

/**
 * @brief Whether the Instances window is drawn at all.
 *
 * This is the lock gate, and it is a function rather than a condition inside
 * the drawing loop so that it can be tested without a GL context. The rule is
 * one line: the catalogue is drawn only while the store is unlocked.
 *
 * The reason it is the whole catalogue and not just the accounts is that every
 * read of the store is a decryption. Instances and folders were never secrets
 * on their own, but they live in a file that is now unreadable without the
 * master password, so while it is not held there is genuinely nothing to draw.
 * The alternative -- showing a catalogue that cannot be written to -- would let
 * an operator edit rows that would then fail to save, which is worse than
 * asking for the password first.
 *
 * @c absent and @c locked both close the gate. @c absent is the first run,
 * where the operator has to create a password before there is a store worth
 * opening; @c failed closed it again after a refusal. Nothing here reports an
 * error, because a closed lock is a state and not a fault.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
shows_instances(const render_input& input) noexcept;

}

#endif
