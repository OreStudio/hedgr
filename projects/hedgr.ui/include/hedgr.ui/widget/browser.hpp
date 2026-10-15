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
#ifndef HEDGR_UI_WIDGET_BROWSER_HPP
#define HEDGR_UI_WIDGET_BROWSER_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/widget/browser_state.hpp"
#include "hedgr.ui/widget/label_pill.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief Whether @p haystack contains @p needle, ignoring Latin case.
 *
 * The one filtering rule the tree and the list both use, so a search box that
 * narrows the tree to a folder also shows that folder's matching leaves rather
 * than a different set. An empty needle matches everything, which is what makes
 * an untouched box mean "no filter" rather than "nothing".
 */
[[nodiscard]] HEDGR_UI_EXPORT bool filter_matches(std::string_view haystack,
    std::string_view needle);

/**
 * @brief Whether @p haystack contains any of @p needles.
 *
 * A row is kept when any field of it matches, because a person typing "prod"
 * into an instance list means "the production one" and not "the one whose name,
 * address and prefix all contain it".
 */
[[nodiscard]] HEDGR_UI_EXPORT bool filter_matches_any(
    const std::vector<std::string_view>& haystacks, std::string_view needle);

/**
 * @brief What a tree row is, and therefore which affordances it can offer.
 *
 * The discriminator is carried rather than inferred from the id, because a
 * folder and a leaf may share an id and the tree must not care.
 */
enum class node_kind : std::uint8_t {
    /** @brief A folder in the tree. */
    folder = 0,
    /** @brief A leaf: an instance, an account, whatever the host browses. */
    leaf = 1,
};

/**
 * @brief The actions a tree row may offer.
 *
 * A value rather than a set of conditions at the call sites, so the rule that
 * root has no rename, move or delete is stated once, in @c affordances_for, and
 * can be tested without a GL context.
 */
struct node_affordances final {
    /** @brief Whether the row can be renamed in place. */
    bool rename = true;
    /** @brief Whether the row can be dragged into another folder. */
    bool move = true;
    /** @brief Whether the row can be removed. */
    bool remove = true;
    /** @brief Whether folders may be created inside it. */
    bool create_inside = true;
};

/**
 * @brief What one tree row is allowed to offer.
 *
 * Root has no rename, no move and no delete, and those affordances are not
 * drawn at all rather than drawn disabled. A disabled control says "you may not
 * do this yet"; the truth about root is that the action does not exist, because
 * the root is what the tree is anchored by and renaming it would rename the
 * tree. Drawing three permanently grey icons on one row teaches a reader that
 * this application sometimes withholds things, which is a worse lesson than an
 * empty space.
 *
 * The rule is enforced in the runtime as well, because the interface is not the
 * only caller that could ask.
 */
[[nodiscard]] HEDGR_UI_EXPORT node_affordances
affordances_for(node_kind kind, bool is_root) noexcept;

/**
 * @brief Whether @p target may receive a drop of @p dragged.
 *
 * A folder cannot be dropped into itself or into anything below it, and the
 * refusal is decided here, before a drop is attempted, rather than after: a
 * drop that is accepted and then refused leaves the row somewhere it must be
 * dragged back from, and the operator is never told which of the two happened.
 *
 * The forbidden set is the dragged folder's own id followed by its descendants,
 * which is what the host's folder table can answer in one call.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool can_drop_onto(std::string_view dragged_id,
    std::string_view target_id, const std::vector<std::string>& forbidden);

/** @brief One folder in the tree the browser draws. */
struct tree_folder final {
    /** @brief Stable id, used as the ImGui id so expansion survives a frame. */
    std::string id;

    /** @brief Containing folder's id. Empty only for a tree's root. */
    std::string parent_id;

    /** @brief Shown text. */
    std::string name;

    /** @brief Whether this is the tree's root. */
    bool is_root = false;
};

/** @brief One leaf of the tree: the row the host browses. */
struct tree_leaf final {
    /** @brief Stable id. */
    std::string id;

    /** @brief Containing folder's id. */
    std::string parent_id;

    /** @brief Shown text in the tree and, by default, in the list. */
    std::string name;

    /**
     * @brief The entry's glyph, or empty.
     *
     * Drawn in the tree and nowhere else. There is one set of icons for an
     * entry: the tree has always drawn one, the list used to draw the same one
     * again beside it, and an entry wearing the same face twice in one window
     * reads as two entries rather than as one. The list is name, host, subject
     * and labels; the glyph is what the tree is for.
     */
    const char* icon = nullptr;

    /**
     * @brief The colour the glyph is drawn in, or zero for the ordinary text
     * colour.
     *
     * The state of the entry, which is why the glyph is worth having at all: an
     * instance's reachability and readiness are one colour on one icon rather
     * than a column of their own. The host decides what the colour means; the
     * browser only paints the glyph with it and leaves the name in the ordinary
     * colour, because a whole row in the state's colour is a row that shouts.
     */
    std::uint32_t icon_colour = 0;

    /**
     * @brief What the glyph's colour means, as a tooltip on the glyph.
     *
     * The low-level cause belongs with the state it explains, and the state is
     * the glyph: hovering the icon that is amber is how a reader asks why it is
     * amber. It travels as text rather than as a callback because it is a
     * sentence the host already has.
     */
    std::string icon_tooltip;

    /**
     * @brief Extra text the filter also reads, beyond the name and the cells.
     *
     * An empty string when the leaf has nothing more to say. Filled by the host
     * with the fields a person would search by and would not want to see, such
     * as an instance's address.
     */
    std::string search_text;
};

/** @brief How wide a list column is, which is all the browser needs to know. */
enum class column_width : std::uint8_t {
    /** @brief Sized to its contents. */
    fit = 0,
    /** @brief Shares what is left. */
    stretch = 1,
};

/** @brief One column of the list, described by the host. */
struct list_column final {
    /** @brief Header text, or empty for an unlabelled column. */
    const char* label = "";

    /** @brief How ImGui should size it. */
    column_width width = column_width::fit;
};

/**
 * @brief One row's cells, as the browser receives them.
 *
 * Reused across rows: the host clears and refills it, so a list of a thousand
 * rows allocates for one row's strings rather than for a thousand.
 */
struct row_cells final {
    /** @brief One string per column, in column order. */
    std::vector<std::string> texts;

    /**
     * @brief A colour per column with one, in column order.
     *
     * A plain 32-bit ARGB value rather than an ImGui colour type, so this
     * header names no ImGui type and the widget's interface stays the two
     * strings a host understands. The host converts from the theme.
     *
     * The first entry tints the row's leading icon, which is how a host says
     * what state the row is in without a column of its own: one icon carrying
     * the state beats a status glyph beside a status glyph. A later entry
     * colours that column's text. The browser does not know what either colour
     * means; the host does.
     */
    std::vector<std::uint32_t> colours;

    /** @brief One cell tooltip per column, empty for none. */
    std::vector<std::string> tooltips;

    /** @brief Clears all three, keeping their capacity. */
    void clear() {
        texts.clear();
        colours.clear();
        tooltips.clear();
    }
};

/**
 * @brief What the browser is browsing: its tree and its list, as the host sees
 * them.
 *
 * The browser is generic over its leaf type through this struct rather than
 * through a template, because the Instances window and the Accounts window must
 * share one compiled widget: a template would put two copies of the drawing
 * code in the library and push the browser's implementation into every
 * translation unit that hosts it.
 *
 * The host answers a handful of questions and, when an action is taken, changes
 * the model. A tree is read as two flat lists -- folders and leaves -- rather
 * than as nested rows, because that is the shape the state tables hold and
 * rebuilding a nesting every frame would allocate for no gain.
 */
struct browser_model final {
    /** @brief Every folder of the tree, including the root. */
    std::vector<tree_folder> folders;

    /** @brief Every leaf of the tree. */
    std::vector<tree_leaf> leaves;

    /** @brief The list's columns. */
    std::vector<list_column> columns;

    /**
     * @brief Fills @p cells with one row's contents.
     *
     * Called once per visible row per frame, so it must not allocate anything
     * it does not have to: the tables it reads are already in memory and the
     * cells are compact text.
     */
    void (*row_cells)(const tree_leaf&, const hedgr::ui::render::render_input&,
        widget::row_cells&) = nullptr;

    /**
     * @brief Fills @p cells with one row's labels, one entry per column.
     *
     * A separate callback from @c row_cells because a label is not a cell
     * value: it has a colour and a severity that the palette and the
     * destructive-action confirmation both read, and the browser draws it as a
     * pill rather than as text. The host leaves the entry for a column it has
     * no labels for empty, and the browser then draws that column as ordinary
     * text. A host with no label columns leaves the callback null.
     */
    void (*row_labels)(const tree_leaf&,
        const hedgr::ui::render::render_input&,
        std::vector<std::vector<label_pill>>&) = nullptr;

    /*
     * The host's callbacks all take the whole window state rather than the
     * browser's own scratch, because what a form collects is the host's: an
     * address, a principal, a wire format. The browser knows a name and a
     * folder, and handing it the rest of the window's state would be a way for
     * the widget's interface to grow fields it has no opinion about.
     */

    /**
     * @brief Draws the host's own actions, in the browser's one toolbar.
     *
     * The window is one strip beside the tree, and it carries both what the
     * browser can do to a tree -- add, edit, duplicate, remove -- and what the
     * host can do to the thing being browsed. Those are two sets of actions on
     * one row, so the host draws its share here rather than in a strip of its
     * own, which is what made two toolbars out of one window.
     *
     * The callback is responsible for the separator between the two sets and
     * for the @c SameLine spacing between its own buttons; it is called where
     * the strip's own buttons have already been submitted, so the row is
     * already open.
     *
     * @param input everything the host's own actions are allowed to read.
     * @param window the hosting window's state, which is what its actions act
     * on: which row is selected, which form is open.
     */
    void (*window_actions)(const hedgr::ui::render::render_input&,
        hedgr::ui::render::window_ui_state&) = nullptr;

    /**
     * @brief The store's lock, when the host wants the control in the toolbar.
     *
     * Drawn at the end of the strip, after the host's own actions, because it
     * is not an action on the thing being browsed: it is the state of the store
     * everything on the window is read from, and it belongs at the edge rather
     * than among the buttons that change a row.
     *
     * Null when the host does not show it, which is what stops every browser
     * acquiring a lock whether or not it has a store behind it.
     */
    hedgr::ui::render::lock_control* lock = nullptr;

    /** @brief Starts the host's form for a new leaf inside a folder. */
    void (*create_leaf)(hedgr::ui::render::window_ui_state&,
        std::string_view parent_id) = nullptr;

    /** @brief Starts the host's form for the selected leaf. */
    void (*edit_leaf)(hedgr::ui::render::window_ui_state&,
        std::string_view leaf_id) = nullptr;

    /** @brief Starts the host's form for a copy of the selected leaf. */
    void (*duplicate_leaf)(hedgr::ui::render::window_ui_state&,
        std::string_view leaf_id) = nullptr;

    /** @brief Draws the host's form for one leaf, when one is open. */
    void (*leaf_form)(const hedgr::ui::render::render_input&,
        hedgr::ui::render::window_ui_state&) = nullptr;

    /** @brief Draws the host's confirmation for removing the selection. */
    void (*remove_form)(const hedgr::ui::render::render_input&,
        hedgr::ui::render::window_ui_state&) = nullptr;

    /**
     * @brief Draws the host's sign-in form for the selected leaf.
     *
     * Beside the list rather than in the toolbar, because the sign-in names the
     * account it is signing in and the toolbar does not know which one that is.
     */
    void (*sign_in_form)(const hedgr::ui::render::render_input&,
        hedgr::ui::render::window_ui_state&) = nullptr;

    /**
     * @brief Whether a reparent is allowed, as the host's table sees it.
     *
     * The tree cannot answer this itself: whether a folder may move under
     * another depends on the whole folder table's ancestry, which the host has
     * and the widget deliberately does not. A missing callback refuses every
     * move, which is the safe default rather than a permissive one.
     */
    bool (*can_reparent)(const hedgr::ui::render::render_input& input,
        std::string_view id, node_kind kind,
        std::string_view new_parent_id) = nullptr;

    /**
     * @brief Whether the browser's actions may be taken at all.
     *
     * False while the store is locked. The actions are drawn and disabled
     * rather than hidden, because the whole surface is already hidden in that
     * state: a browser on screen with no buttons would read as a browser that
     * has lost its buttons.
     */
    bool can_write = false;

    /**
     * @brief Whether the toolbar offers the copy action.
     *
     * True for the Accounts window, where copying an account is a real thing to
     * do -- two accounts on one instance that differ by one field. False for
     * the Instances window, where a second deployment of the same broker with
     * the same prefix is not a copy of the first but a duplicate, and the store
     * refuses it anyway because an instance's name is unique.
     */
    bool can_duplicate = false;

    /**
     * @brief Whether the toolbar offers the per-selection refresh.
     *
     * True for the Instances window, whose leaves are the things a probe
     * measures, and false for the Accounts window, where the same button would
     * have nothing to measure.
     */
    bool can_refresh = false;
};

/**
 * @brief Draws one browser: a folder tree on the left, a list on the right.
 *
 * Must be called inside an already-begun ImGui window. The tree is rebuilt from
 * the model every frame, which is what lets a filter narrow it and a drag
 * re-parent it without any state on this side; the expansion, which is the one
 * piece of tree state worth keeping, is ImGui's, keyed by the stable ids the
 * model supplies.
 *
 * The list reports on the selected tree node and holds no selection of its own,
 * so there is exactly one answer to "what is selected" and no way for the tree
 * and the list to disagree about it.
 *
 * @param model The host's view of what is being browsed.
 * @param window The hosting window's state, which is where the browser's own
 * scratch lives and what the host's form callbacks are handed.
 * @param input Everything the host's callbacks are allowed to read.
 */
HEDGR_UI_EXPORT void render_browser(const browser_model& model,
    hedgr::ui::render::window_ui_state& window,
    const hedgr::ui::render::render_input& input);

}

#endif
