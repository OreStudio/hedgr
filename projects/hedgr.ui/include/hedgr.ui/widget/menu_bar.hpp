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
#ifndef HEDGR_UI_WIDGET_MENU_BAR_HPP
#define HEDGR_UI_WIDGET_MENU_BAR_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/instances_panel.hpp"
#include "hedgr.ui/widget/menu.hpp"
#include "imgui.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief The password form the ACCOUNTS menu opens for an account with none
 * memorised.
 *
 * It is the same three facts the Accounts window's form uses -- who, on which
 * deployment, and the typed password -- kept here because the menu owns the
 * prompt rather than navigating to a window. The buffer is fixed so typing
 * allocates nothing and is wiped when the form closes.
 */
struct account_sign_in_state final {
    std::string instance_id;
    std::string instance_name;
    std::string account_id;
    std::string principal;
    std::array<char, 128> password{};
    bool remember = false;
    bool open = false;
    bool focus = false;
};

/** @brief The scratch the ACCOUNTS menu rebuilds on each frame it draws. */
struct accounts_menu_state final {
    /** @brief Slab indices of the instances that have at least one account. */
    std::vector<std::uint32_t> environments;

    /** @brief Slab indices of the folders of the open environment, in draw order. */
    std::vector<std::uint32_t> folders;

    /** @brief Slab indices of the accounts of the open environment, in draw order. */
    std::vector<std::uint32_t> accounts;
};

/**
 * @brief Which top-level menu of the bar is open, if any.
 *
 * One value for the whole bar, because at most one popup hangs off it. The
 * index is the entry's place in the bar, which the caller picks and keeps
 * stable.
 */
struct menu_bar_state final {
    /** @brief The value of @c open when no menu is. */
    static constexpr int none = -1;

    int open = none;

    /** @brief Whether the About window is showing. */
    bool about_open = false;

    /** @brief The Instances panel's filter and filtered list. */
    instances_panel_state instances;

    /** @brief The ACCOUNTS menu's per-environment scratch. */
    accounts_menu_state accounts;

    /** @brief The password form an account without a memorised one opens. */
    account_sign_in_state sign_in;
};

/**
 * @brief Where a panel @p width wide goes, given its label's left edge.
 *
 * Flush with the label unless that runs past @p max_x, when it slides left to
 * end there, and never left of @p min_x.
 */
[[nodiscard]] constexpr float menu_panel_left(float label_left, float width,
    float min_x, float max_x) noexcept {
    const float right_fit = max_x - width;
    const float left = label_left < right_fit ? label_left : right_fit;
    return left > min_x ? left : min_x;
}

/**
 * @brief Sizes and places a large panel under the label @p min to @p max, and
 * returns its size.
 *
 * The @p preferred size is capped to what is left of the main viewport's work
 * area below the label, less a margin, so a small window gets a smaller panel
 * rather than one that runs off the screen. The position is set for the next
 * window, sliding left if the panel would pass the work area's right edge.
 */
HEDGR_UI_EXPORT ImVec2 place_menu_panel(ImVec2 min, ImVec2 max,
    ImVec2 preferred);

/**
 * @brief The open menu after one entry has been looked at, as a pure rule.
 *
 * A press opens the entry, or closes it when it is the open one. While some
 * menu is open, the pointer on another entry moves the menu there, which is
 * how the reference's bar behaves once a menu has been clicked open.
 *
 * @p popup_alive says whether ImGui still holds the popup of @p open. A click
 * outside a popup, on its own entry included, closes it at the press, before
 * the release that @p pressed reports, so an open entry whose popup is gone had
 * that press spent on closing it and must not open again.
 */
[[nodiscard]] constexpr int menu_after_entry(int open, int entry,
    bool popup_alive, bool pressed, bool hovered) noexcept {
    if (open == entry && !popup_alive) {
        open = menu_bar_state::none;
        pressed = false;
    }
    if (pressed)
        return open == entry ? menu_bar_state::none : entry;
    if (hovered && open != menu_bar_state::none && open != entry)
        return entry;
    return open;
}

/**
 * @brief Applies the last item as entry @p entry of the bar and opens or
 * closes the popup @p popup_id to match.
 *
 * Call it right after the entry's item, so the last item is the entry: the
 * popup is anchored under it. Returns whether the entry's popup is the open
 * one, which is the caller's cue to draw its rows in a @c menu_popup of the
 * same id.
 */
HEDGR_UI_EXPORT bool menu_bar_entry(menu_bar_state& state, int entry,
    const char* popup_id, bool pressed);

/**
 * @brief A top-level label and its menu: the one call a bar needs per menu.
 *
 * @p rows draws the menu's rows and runs only while the menu is open.
 * @p height is the label's height, as for @c menu_label. A @p panel with a
 * positive width asks for a large fixed-size panel of about that size, capped
 * by @c place_menu_panel, instead of one as wide as its rows.
 */
template<class Rows>
void menu_bar_label(menu_bar_state& state, int entry, const char* text,
    Rows&& rows, float height = 0.0f, ImVec2 panel = ImVec2(0.0f, 0.0f)) {
    const bool pressed = menu_label(text, state.open == entry, height);
    if (menu_bar_entry(state, entry, text, pressed)) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 size = panel.x > 0.0f ? place_menu_panel(min, max, panel)
                                           : ImVec2(0.0f, 0.0f);
        const menu_popup popup(text, size.x, (min.x + max.x) * 0.5f, size.y);
        if (popup)
            rows();
    }
}

}

#endif
