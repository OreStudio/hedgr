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
#ifndef HEDGR_UI_WIDGET_CARDS_HPP
#define HEDGR_UI_WIDGET_CARDS_HPP

#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief One card, as a pane assembles it before it is drawn.
 *
 * A value rather than a drawing call, because a pane decides its topics from
 * the data before it draws any of them: a topic with one card is not a topic,
 * and which topics survive that rule is known only once every card has been
 * collected. Both windows that show a selected record build this same value, so
 * the wall they draw is one wall rather than two that look alike today.
 */
struct card_spec final {
    /** @brief ImGui id and capture name, unique among the pane's cards. */
    const char* id = "";

    /** @brief The glyph that names the field, or null for none. */
    const char* glyph = nullptr;

    /** @brief The card's title line. */
    const char* title = "";

    /** @brief The value. */
    std::string value;

    /** @brief The muted line under the value. */
    std::string caption;

    /** @brief The value's colour, or zero for the ordinary text colour. */
    std::uint32_t colour = 0;

    /**
     * @brief Whether the card takes the pane's whole width.
     *
     * A path and a failure sentence are each one long value. Sharing a column
     * clips them to a sliver, and a path a reader cannot see the end of is a
     * path they cannot check, which is the whole reason the card shows it. A
     * card that holds one says so here and the wall gives it a line of its own.
     * Everything else keeps sharing a row, because a wall of full width cards
     * is a list and stops being a wall.
     */
    bool full_width = false;

    /**
     * @brief Whether the value wraps onto more than one line.
     *
     * A card's value is one line by default, because a card is a line of text
     * and a value longer than its box scrolls rather than growing the box. The
     * failure sentence is the exception, and it is already written as more than
     * one line, so a single line field would show the headline and hide the
     * cause on the line beneath it. A wrapping card is measured and given the
     * height its text needs, so the whole sentence is read at once. It implies
     * a full width card, because a wrapped sentence in a column would make one
     * row of the wall taller than the rest for no gain.
     */
    bool wrap = false;
};

/** @brief One topic of a pane: a heading and the cards under it. */
struct topic_spec final {
    /** @brief The heading's text. */
    const char* title = "";

    /** @brief The cards, in the order they are drawn. */
    std::vector<card_spec> cards;
};

/** @brief The gap between two cards. */
inline constexpr float card_gap = 8.0f;

/** @brief The air inside a card, on every side. */
inline constexpr float card_padding = 10.0f;

/**
 * @brief The width below which a card stops being a card.
 *
 * A card holds a title, a value and a caption, and the longest of those is a
 * certificate path. Below this width the value has nowhere to be read and the
 * wall drops a column rather than drawing a column of slivers.
 */
inline constexpr float card_min_width = 250.0f;

/** @brief How many cards fit across a pane of @p pane_width. */
[[nodiscard]] HEDGR_UI_EXPORT int card_columns(float pane_width);

/** @brief The width of one card in a grid of @p columns. */
[[nodiscard]] HEDGR_UI_EXPORT float card_width(
    float pane_width, int columns);

/**
 * @brief The height of one card, from the three lines it holds.
 *
 * The title, the value and the caption, measured from the font so a theme that
 * installs a larger face gets cards that still hold their own text. Every card
 * in the wall is this tall, which is what makes the wall a grid rather than a
 * ragged set of boxes.
 */
[[nodiscard]] HEDGR_UI_EXPORT float card_height();

/**
 * @brief Moves to the next slot of the wall, wrapping a row at @p columns.
 *
 * The gap is passed to SameLine rather than left to the style, because the
 * style's item spacing also spaces a card from the row above it and the two
 * distances do not have to agree.
 */
HEDGR_UI_EXPORT void card_slot(int& column, int columns);

/**
 * @brief Opens one card and fills its own surface.
 *
 * A child rather than a rectangle into the draw list, because the card holds
 * live widgets and a widget needs a window of its own to be clipped by. The
 * child never scrolls: a card is a box of a fixed size and a value that does
 * not fit is scrolled by its own input rather than by growing the box.
 *
 * A marked card takes the edit colour on its outline and a bar down its inner
 * left edge. The bar is the shape the rail's chosen row carries, in the colour
 * of a different fact, so an unsaved field is recognisable as a mark rather
 * than as a differently coloured box.
 *
 * @return Whether the card is visible and its body should be submitted. The
 * caller has to call @c end_card either way.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool begin_card(
    const char* id, float width, float height, bool marked);

/** @brief Closes a card, whether or not its body was submitted. */
HEDGR_UI_EXPORT void end_card();

/** @brief Writes a card's title line, with its glyph where it has one. */
HEDGR_UI_EXPORT void card_title(
    const char* glyph, std::string_view title, ImU32 colour);

/** @brief Draws one read-only card of the wall. */
HEDGR_UI_EXPORT void draw_card(
    const card_spec& card, float width, float height);

/**
 * @brief Draws a whole wall of topics.
 *
 * The wall's rules are stated once here rather than in each pane: a topic with
 * two cards or more takes its heading and a topic with one folds into the row
 * above it, because the heading names a set and a set of one is not a set. A
 * full width card is given a line of its own, so a row part drawn ends there
 * and the next card opens a new row.
 *
 * @param topics The topics, in the order they are drawn.
 * @param pane_width The width one full width card takes.
 * @param width The width one column card takes.
 * @param height The height of one card.
 * @param columns How many columns share a row.
 */
HEDGR_UI_EXPORT void draw_topics(const std::vector<topic_spec>& topics,
    float pane_width, float width, float height, int columns);

}

#endif
