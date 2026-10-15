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
#ifndef HEDGR_UI_ICON_ICONS_HPP
#define HEDGR_UI_ICON_ICONS_HPP

#include "hedgr.ui/icon/third_party/IconsFontAwesome6.h"

/**
 * @brief The whole icon set, as semantic names.
 *
 * This is the only translation unit boundary the icon font has: no other file
 * in the component includes IconsFontAwesome6.h, and no other file names a
 * Font Awesome constant. The set is therefore swappable by editing this file
 * alone -- point each name at a different constant, or at a glyph from an
 * entirely different font, and every toolbar, table row and status indicator
 * follows, because the rest of the interface only ever says "the connect icon"
 * rather than "the plug icon".
 *
 * The string literals are compile-time constants and have static storage
 * duration, which is what ImGui requires of a glyph range or a glyph offset:
 * it stores the pointer rather than copying the text.
 */
namespace hedgr::ui::icon {

/** @brief An instance: a deployment Hedgr can talk to. */
inline constexpr const char* instance = ICON_FA_SERVER;

/** @brief An account: a principal that may be presented to an instance. */
inline constexpr const char* account = ICON_FA_ID_CARD;

/** @brief A session, in the abstract: one account presented to one instance. */
inline constexpr const char* session = ICON_FA_CIRCLE_NODES;

/** @brief A session whose connection attempt is still in flight. */
inline constexpr const char* session_pending = ICON_FA_USER_CLOCK;

/** @brief A session whose last attempt failed. */
inline constexpr const char* session_failed = ICON_FA_USER_XMARK;

/** @brief Open a session. */
inline constexpr const char* connect = ICON_FA_PLUG;

/** @brief Close a session. */
inline constexpr const char* disconnect = ICON_FA_PLUG_CIRCLE_XMARK;

/** @brief Add a row. */
inline constexpr const char* add = ICON_FA_PLUS;

/** @brief Remove a row, or ask to: this is the destructive action. */
inline constexpr const char* remove = ICON_FA_TRASH;

/** @brief Edit a row in place. */
inline constexpr const char* edit = ICON_FA_PEN;

/** @brief Confirm and persist an edit. */
inline constexpr const char* save = ICON_FA_FLOPPY_DISK;

/** @brief Abandon an edit. The X of a form, not the X of a window. */
inline constexpr const char* cancel = ICON_FA_XMARK;

/** @brief A chart: a timeline of one numeric series. */
inline constexpr const char* chart = ICON_FA_CHART_LINE;

/** @brief A grid: a paged table of one resource. */
inline constexpr const char* grid = ICON_FA_TABLE_CELLS;

/** @brief A notice: a message for a person to read. */
inline constexpr const char* notice = ICON_FA_BELL;

/** @brief Fetch the current data again, now. */
inline constexpr const char* refresh = ICON_FA_ROTATE;

/** @brief Settings. */
inline constexpr const char* settings = ICON_FA_GEAR;

/** @brief Go full screen, or leave it. */
inline constexpr const char* fullscreen = ICON_FA_EXPAND;

/** @brief Leave the application. */
inline constexpr const char* exit_app = ICON_FA_POWER_OFF;

/** @brief Something is not fatal but wants attention. */
inline constexpr const char* warning = ICON_FA_TRIANGLE_EXCLAMATION;

/** @brief Something did not work. */
inline constexpr const char* error = ICON_FA_CIRCLE_EXCLAMATION;

/** @brief Something worth knowing, with no urgency attached. */
inline constexpr const char* information = ICON_FA_CIRCLE_INFO;

/** @brief Reveal a secret that is currently masked. */
inline constexpr const char* eye = ICON_FA_EYE;

/** @brief Mask a secret that is currently revealed. */
inline constexpr const char* eye_off = ICON_FA_EYE_SLASH;

/** @brief Filter a list down to what matches some text. */
inline constexpr const char* search = ICON_FA_MAGNIFYING_GLASS;

/** @brief The Instance Manager: the root surface of the interface. */
inline constexpr const char* dashboard = ICON_FA_CIRCLE_PLAY;

/** @brief A point in time, or an elapsed time. */
inline constexpr const char* clock = ICON_FA_CLOCK;

/** @brief A connection between two things. */
inline constexpr const char* link = ICON_FA_LINK;

/**
 * @brief A probe: whether the deployment answers at all.
 *
 * A filled circle, because a reachability is a dot beside an instance's name
 * rather than an action: the colour carries the answer and the glyph only has
 * to be the same shape on every row so the column reads as a column.
 */
inline constexpr const char* probe = ICON_FA_CIRCLE;

/**
 * @brief Readiness: whether the deployment has been bootstrapped.
 *
 * A ticked circle rather than a plain one, so the two indicators beside an
 * instance's name are told apart by shape as well as by colour: one reports on
 * the broker, the other on the service behind it, and a reader who cannot
 * distinguish red-green still has to be able to tell which is which.
 */
inline constexpr const char* ready = ICON_FA_CIRCLE_CHECK;

/*
 * Paging, for a paged table. The spec's list of semantic names stops at link;
 * these four are added because a paged grid's controls are icons here like
 * every other action, and an icon must come from this file rather than being
 * spelled inline at the call site.
 */

/** @brief Jump to the first page. */
inline constexpr const char* page_first = ICON_FA_ANGLES_LEFT;

/** @brief Go back one page. */
inline constexpr const char* page_previous = ICON_FA_ANGLE_LEFT;

/** @brief Go forward one page. */
inline constexpr const char* page_next = ICON_FA_ANGLE_RIGHT;

/** @brief Jump to the last page. */
inline constexpr const char* page_last = ICON_FA_ANGLES_RIGHT;

}

#endif
