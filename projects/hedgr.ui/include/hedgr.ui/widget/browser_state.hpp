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
#ifndef HEDGR_UI_WIDGET_BROWSER_STATE_HPP
#define HEDGR_UI_WIDGET_BROWSER_STATE_HPP

#include <array>
#include <cstdint>
#include <string>

namespace hedgr::ui::widget {

/**
 * @brief Which form, if any, is open over the browser.
 *
 * One value rather than a boolean per form, because exactly one form is open at
 * a time and two booleans would allow a state that cannot happen.
 */
enum class form_kind : std::uint8_t {
    /** @brief No form is open. */
    none = 0,
    /** @brief Naming a new folder. */
    folder = 1,
    /** @brief Naming an existing folder, or a new one inside it. */
    leaf = 2,
};

/**
 * @brief A window's browser: its selection, its filter, and its open form.
 *
 * Interface scratch and nothing else. Expansion is not here: ImGui owns it,
 * keyed by the stable id the browser gives each tree row, which is what keeps a
 * folder open across frames without this struct having to remember it. Nothing
 * here is persisted, so a restarted Hedgr opens with the root selected and the
 * tree as ImGui left it.
 *
 * The selection is a folder and a leaf whose meanings are fixed by one rule:
 * a leaf selection wins, and selecting a folder clears it. That is what "the
 * list reports on the selected tree node rather than holding a second
 * selection" comes to in code -- there is never a moment where a folder and a
 * leaf are both selected, so there is no way to draw a list belonging to
 * neither.
 */
struct browser_state final {
    /** @brief The selected folder's id, or empty for none. */
    std::string selected_folder;

    /**
     * @brief The selected leaf's id, or empty for none.
     *
     * Non-empty only while no folder is selected. The list shows this one leaf
     * and the tree draws it as selected.
     */
    std::string selected_leaf;

    /** @brief Filter text, as typed. */
    std::array<char, 128> filter{};

    /** @brief Which form is open. */
    form_kind form = form_kind::none;

    /** @brief The folder the open form belongs to, or the one being renamed. */
    std::string form_folder;

    /** @brief The leaf the open form is editing, empty when creating one. */
    std::string form_leaf;

    /**
     * @brief The leaf a copy is being made from, empty when none is.
     *
     * The row itself is the host's to read, so the browser carries the key and
     * the host fills the form from it on the frame after the request. It is
     * separate from @c form_leaf because an edit and a copy differ in what they
     * name, not in what is open.
     */
    std::string duplicate_source;

    /** @brief Whether the open form is making a copy rather than an edit. */
    bool form_duplicating = false;


    /** @brief The name typed into the folder or leaf form. */
    std::array<char, 128> form_name{};

    /**
     * @brief The labels typed into the leaf form, comma separated.
     *
     * Names rather than ids, because a person attaches a label by typing the
     * word they gave it, and an id is not something they have ever seen.
     */
    std::array<char, 128> form_labels{};

    /** @brief Whether the selected folder's removal is being confirmed. */
    bool confirm_remove_folder = false;

    /** @brief Whether the selected leaf's removal is being confirmed. */
    bool confirm_remove_leaf = false;

    /** @brief Whether the selection's password is drawn in the clear. */
    bool password_visible = false;

    /**
     * @brief The credential typed for the selected leaf's sign-in.
     *
     * Fixed size because it is handed to ImGui::InputText, which edits a
     * caller-owned buffer in place. Cleared as soon as it is used.
     */
    std::array<char, 128> credential{};

    /** @brief Whether the sign-in form is asking for a password. */
    bool sign_in_open = false;

    /**
     * @brief The instance an accounts browser belongs to.
     *
     * Set by the host at the top of its draw, so that a callback which was
     * handed no tables still knows whose accounts it is looking at. It is not
     * the tree: the tree comes from the folder table, and this is only the key
     * the host needs to look rows up by.
     */
    std::string instance;

    /**
     * @brief The instance a form's row should be created for.
     *
     * A form for a new account has no row to read its instance from, so the
     * host leaves it here when it opens the form.
     */
    std::string form_owner;
};

}

#endif
