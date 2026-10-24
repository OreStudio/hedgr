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
#ifndef HEDGR_UI_WIDGET_VIEW_STATE_HPP
#define HEDGR_UI_WIDGET_VIEW_STATE_HPP

#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include <cstdint>
#include <optional>
#include <string_view>

namespace hedgr::ui::widget {

/**
 * @brief What a window's body says when it has nothing to draw: the UX language.
 *
 * This is the one place the meaning of the marks is written down. A window
 * never words one of these states itself. It fills a @c view_facts, asks
 * @c view_state_of, and draws the answer with @c draw_view_state or one of the
 * helpers below. The words live only in the tooltip over the mark and in the
 * one word on the window's foot. docs/plans/ux-language.md carries the same
 * table for readers outside the code.
 *
 * | state         | meaning                                   | mark                    | foot     |
 * |---------------|-------------------------------------------|-------------------------|----------|
 * | none          | there is content                          | the content             | its own  |
 * | reading       | a connection or a read is in flight       | a spinner, no words     | updating |
 * | not_connected | no usable session: never signed in, no    | broadcast arcs with a   | OFFLINE  |
 * |               | session bound, disconnected, the login    | slash through them,     |          |
 * |               | ended, or signed in with no token         | dimmest grey            |          |
 * | error         | the session is up or tried and something  | a ringed exclamation    | ERROR    |
 * |               | failed: a failed connection, a refused or | mark, one grey step     |          |
 * |               | undecodable read, history unavailable,    | brighter, no red        |          |
 * |               | a series not in the catalogue, no answer  |                         |          |
 * | no_data       | connected, no errors, and what was read   | empty chart axes with a | NO DATA  |
 * |               | is empty: no points, no rates, nothing    | dashed flat line, dim   |          |
 * |               | installed, no match for a filter          |                         |          |
 *
 * The states are exclusive and ordered by what an operator can do about them.
 * Not connected means act on the session, error means read the reason, no data
 * means nothing is wrong.
 */
enum class view_state : std::uint8_t {
    /** @brief There is content: the window draws it. */
    none,
    /** @brief A connection or a read is in flight. */
    reading,
    /** @brief There is no usable session. */
    not_connected,
    /** @brief Something failed, and the reason says what. */
    error,
    /** @brief Connected, no errors, and what was read is empty. */
    no_data,
};

/** @brief Which sentence a state's tooltip opens with. */
enum class view_words : std::uint8_t {
    /** @brief No tooltip: the state is none or reading. */
    none,
    /** @brief No session was ever opened, or it was closed. */
    not_connected,
    /** @brief The session this window used is gone. */
    session_ended,
    /** @brief The session's last connection attempt failed. */
    connection_failed,
    /** @brief A read on an open session failed. */
    read_failed,
    /** @brief Connected, and the thing read is empty. */
    empty,
};

/**
 * @brief Everything @c view_state_of needs to judge one window body.
 *
 * Plain values, borrowed text, no allocation, so a steady frame builds one for
 * free. The session half comes from @c session_view_facts and the content half
 * from the window, which knows what it reads.
 */
struct view_facts final {
    /** @brief The window's session status, or nothing when none is bound. */
    std::optional<hedgr::model::domain::session_status> session;

    /** @brief Whether an authenticated session holds a token. */
    bool has_token = true;

    /**
     * @brief Whether the window was bound to a session that no longer exists.
     *
     * Only read when @c session is empty: it tells a login that ended from one
     * that was never opened.
     */
    bool ended = false;

    /** @brief The session's last connection error, or empty. */
    std::string_view session_error;

    /** @brief Whether the window's own read is outstanding. */
    bool reading = false;

    /** @brief Why the window's own read failed, or empty. */
    std::string_view read_failure;

    /** @brief Whether the read produced anything to draw. */
    bool has_data = true;

    /** @brief What is empty, for the no-data tooltip, or empty. */
    std::string_view empty_what;
};

/** @brief A body's state, which sentence opens its tooltip, and its reason. */
struct view_verdict final {
    view_state state = view_state::none;
    view_words words = view_words::none;

    /** @brief The error's reason, or what is empty. Borrowed from the facts. */
    std::string_view reason;
};

/**
 * @brief Judges a window body from its facts. The one function behind the table.
 *
 * A session that is not usable decides the body whatever the window holds, so
 * the order is session first, then the window's own read, then its failure,
 * then emptiness. Pure and allocation free.
 */
[[nodiscard]] HEDGR_UI_EXPORT view_verdict view_state_of(
    const view_facts& facts) noexcept;

/** @brief A state's tooltip: its opening sentence and the line under it. */
struct view_tooltip final {
    std::string_view caption;
    std::string_view detail;
};

/** @brief The tooltip text for a verdict. Empty for none and reading. */
[[nodiscard]] HEDGR_UI_EXPORT view_tooltip tooltip_of(
    const view_verdict& verdict) noexcept;

/**
 * @brief The one word a foot carries for a state, or empty when it has none.
 *
 * Reading and none have no word of their own: the foot's updating note and the
 * window's own health word cover them.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::string_view view_foot_word(
    view_state state) noexcept;

/**
 * @brief Puts a verdict on a window's foot.
 *
 * Sets the word, the colour and, for an error, the reason as the word's
 * tooltip. A reading verdict only raises the updating note. None leaves the
 * foot alone, so a window's own health word survives.
 */
HEDGR_UI_EXPORT void set_view_foot(
    foot_state& foot, const view_verdict& verdict);

/**
 * @brief Draws a rotating arc of @p radius round @p centre.
 *
 * A moving three quarters of a circle, its start angle taken from the ImGui
 * clock so no state is kept. The caller owns the frame that follows, which has
 * to come while this is on screen for the arc to turn.
 */
HEDGR_UI_EXPORT void draw_spinner(
    ImDrawList* draw, ImVec2 centre, float radius);

/**
 * @brief Draws a verdict's mark in the area @p min to @p max of the current window.
 *
 * A mark of at least @p glyph_size centred in the area, or the spinner. The
 * words are in a tooltip while the pointer is over the area. None draws
 * nothing.
 */
HEDGR_UI_EXPORT void draw_view_state(const view_verdict& verdict, ImVec2 min,
    ImVec2 max, float glyph_size);

/**
 * @brief Draws a verdict in a window's body, above its foot.
 *
 * Sets the foot's word as well, so the mark and the word cannot disagree. The
 * caller draws the foot after it. The mark fills the content area the cursor
 * is in, less the foot's height.
 */
HEDGR_UI_EXPORT void draw_view_state_body(const status_bar& status,
    foot_state& foot, const view_verdict& verdict);

/**
 * @brief Draws a verdict across the rest of the current window or pane.
 *
 * For a detail pane or a child that has no foot of its own.
 */
HEDGR_UI_EXPORT void draw_view_state_pane(const view_verdict& verdict);

/**
 * @brief Draws a verdict in a box of @p size at the cursor, for a popup.
 *
 * The box is an item, so a popup sizes round it.
 */
HEDGR_UI_EXPORT void draw_view_state_inline(
    const view_verdict& verdict, ImVec2 size);

}

#endif
