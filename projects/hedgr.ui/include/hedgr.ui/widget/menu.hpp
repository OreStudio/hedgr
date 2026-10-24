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
#ifndef HEDGR_UI_WIDGET_MENU_HPP
#define HEDGR_UI_WIDGET_MENU_HPP

#include "hedgr.ui/export.hpp"
#include <optional>

namespace hedgr::ui::widget {

/**
 * @brief A popup opened as the flat dark rounded panel of the reference's menu.
 *
 * It pushes the menu look (fill, outline, padding, the purple header colours the
 * rows read) before BeginPopup and pops it after EndPopup, so the look reaches
 * the rows inside and nothing else. List selection elsewhere keeps its navy
 * wash because the colours are pushed here and nowhere global.
 *
 * Use it as the popup's own scope: construct it where BeginPopup would be called
 * and draw the rows only when it converts to true. The popup is opened with
 * ImGui::OpenPopup under the same @p id, as for BeginPopup. A @p width of zero
 * means as wide as the widest row, at least the theme's minimum.
 *
 * With a @p caret_x, the screen x of the label that opened the popup, a small
 * caret is drawn on the panel's top edge pointing up at that x (clamped inside
 * the corners), so the panel reads as hanging off its label. The caret is drawn
 * on the foreground list of the popup's viewport, as it lies outside the window;
 * a popup that is its own OS window gets none.
 *
 * A positive @p height fixes the panel's height too, for a large panel that
 * scrolls its own contents. Zero fits it to its rows.
 */
class HEDGR_UI_EXPORT menu_popup final {
public:
    explicit menu_popup(const char* id, float width = 0.0f,
        std::optional<float> caret_x = std::nullopt, float height = 0.0f);
    ~menu_popup();
    menu_popup(const menu_popup&) = delete;
    menu_popup& operator=(const menu_popup&) = delete;

    /** @brief Whether the popup is open, so its rows are to be drawn. */
    [[nodiscard]] explicit operator bool() const noexcept {
        return open_;
    }

private:
    bool open_;
    std::optional<float> caret_x_;
};

/**
 * @brief The x a caret is centred at: @p label_x, kept clear of a panel's rounded
 * corners, or the panel's middle when it is too narrow to fit one.
 */
[[nodiscard]] HEDGR_UI_EXPORT float menu_caret_centre(float panel_min_x,
    float panel_max_x, float label_x) noexcept;

/** @brief What one menu row says. Every field after the label is optional. */
struct menu_row final {
    /** @brief The row's text, and with the caller's PushID its identity. */
    const char* label = "";

    /**
     * @brief An icon glyph from the icon set, or null.
     *
     * A row with an icon reserves a column for it, so give every row of one
     * menu an icon or none, or the labels will not line up.
     */
    const char* icon = nullptr;

    /** @brief Text right-aligned in the row, such as a key chord, or null. */
    const char* shortcut = nullptr;

    /** @brief Whether the row ends in a chevron, as a row that opens a submenu. */
    bool submenu = false;

    /** @brief Whether the row is the chosen one, drawn as the purple bar. */
    bool selected = false;

    /** @brief A disabled row is dimmed, takes no hover and is skipped by navigation. */
    bool enabled = true;
};

/**
 * @brief Draws one row of a menu and returns whether it was activated.
 *
 * The row is a real ImGui selectable the width of the panel, so the pointer, the
 * arrow keys and a gamepad all reach it, and a click or Enter returns true.
 * A submenu row only draws the chevron: the caller opens its panel beside
 * the row (anchor at ImGui::GetItemRectMax) when this returns true or the row
 * is hovered, as the large Instances and Accounts panels do.
 */
HEDGR_UI_EXPORT bool menu_item_row(const menu_row& row);

/** @brief Draws a rule across the panel between two groups of rows. */
HEDGR_UI_EXPORT void menu_separator();

/**
 * @brief Draws a group's heading: dim, upper case and letter-spaced, with no
 * hover and no navigation, since it is not a row.
 */
HEDGR_UI_EXPORT void menu_heading(const char* text);

/**
 * @brief Draws a top-level menu label, such as FILE, in upper case, and
 * returns whether it was clicked.
 *
 * Bright while hovered or while its menu is @p open, dim otherwise, with no
 * fill. It is an item for navigation too. The caller opens the popup on the
 * click and anchors it at the item's lower left.
 *
 * The item is @p height tall, or one frame tall when that is zero. A bar that
 * wants its popups flush under its own edge passes its own height.
 */
HEDGR_UI_EXPORT bool menu_label(const char* text, bool open,
    float height = 0.0f);

}

#endif
