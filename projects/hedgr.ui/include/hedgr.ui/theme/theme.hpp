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
#ifndef HEDGR_UI_THEME_THEME_HPP
#define HEDGR_UI_THEME_THEME_HPP

#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/export.hpp"
#include "imgui.h"

namespace hedgr::ui::theme {

/**
 * @brief Applies the dark, dense trading-desk style.
 *
 * Call once, after the ImGui context exists. The palette is deliberately
 * muted and semi-transparent so the photographic background reads through it:
 * a panel is a pane of smoked glass over the desk, not an opaque card.
 */
HEDGR_UI_EXPORT void apply_theme();

/**
 * @brief The palette's red, for anything that reports a failure.
 *
 * Every refusal in the interface is drawn in this colour: a refused unlock, an
 * unreachable instance, a failed sign-in, and the runtime's own error line.
 * It is named and exported rather than reached through one of the helpers
 * below, because a failure is not a fact about a lock or a session and the
 * next surface that reports one should not have to pick a neighbour to borrow
 * a colour from. The helpers agree on this value; this is the value.
 */
inline constexpr ImU32 failure_colour = IM_COL32(255, 23, 68, 255);

/**
 * @brief The colour of a session status.
 *
 * Disconnected grey, connecting amber, authenticated green, failed red.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
status_colour(hedgr::model::domain::session_status value) noexcept;

/** @brief The icon naming a session status. */
[[nodiscard]] HEDGR_UI_EXPORT const char*
status_icon(hedgr::model::domain::session_status value) noexcept;

/**
 * @brief The colour of the reachability dot.
 *
 * Grey for unknown, red for unreachable, green for reachable. Grey is not a
 * shade of either answer: it is the colour of a question nobody has asked yet,
 * and drawing it as red would report an outage on a first run.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
reachability_colour(hedgr::model::domain::reachability value) noexcept;

/**
 * @brief The colour of the readiness marker.
 *
 * Grey for unknown, red for a service that is missing, amber for bootstrapping,
 * green for ready. Unknown and service-missing are deliberately different. A
 * probe that has not reported is a question nobody has asked, and a broker that
 * answered with no IAM behind it is a fault, so a reader who sees grey and waits
 * is not waiting for something that is not happening.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
readiness_colour(hedgr::model::domain::readiness value) noexcept;

/*
 * The two below answer a coarser question than either column above: what one
 * row is, in one colour, rather than what one column of it is. A list read at a
 * glance needs one answer per instance, and a mapping spread across the drawing
 * code is a mapping that drifts from this one.
 */

/**
 * @brief The colour of an instance's icon, from what the last probe found.
 *
 * Red for an unreachable broker, a darker red for a broker whose IAM service
 * is missing, amber for bootstrapping, green for ready, and grey for an
 * instance no probe has reported on.
 *
 * The two faults are two colours because they are two problems with two
 * different fixes, and a row that drew them the same would send a reader to the
 * wrong one. The words below name each state exactly, so the tooltip and the
 * pane agree with the colour rather than replacing it.
 *
 * Grey is not a shade of failing: it is what is known before an answer arrives,
 * and painting it red would report an outage nobody measured.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32 probe_state_colour(
    const hedgr::model::domain::probe_result& result, bool observed) noexcept;

/**
 * @brief The short word for that same colour, for a tooltip.
 *
 * "unreachable", "service missing", "bootstrapping", "ready" or "unknown", so
 * the words a reader hovers over agree with the colour they are looking at
 * rather than repeating the state's canonical name. No two states share a word.
 */
[[nodiscard]] HEDGR_UI_EXPORT const char* probe_state_word(
    const hedgr::model::domain::probe_result& result, bool observed) noexcept;

/** @brief The colour a label's name is drawn in. */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
label_colour(hedgr::model::domain::label_colour value) noexcept;

/**
 * @brief A text colour that stays readable on @p background.
 *
 * A label pill fills itself with the label's colour and writes its name on top
 * of that fill, so the text colour is a function of the fill rather than a
 * constant: the palette has both a pale amber and a dark purple in it, and a
 * fixed white would be unreadable on one of them.
 *
 * The choice is the background's luminance against a threshold, because that is
 * the property a reader's eye actually responds to here; a hue-aware contrast
 * ratio would be a more elaborate answer to the same question.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32 text_colour_on(ImU32 background) noexcept;

/** @brief The icon naming a window kind. */
[[nodiscard]] HEDGR_UI_EXPORT const char*
window_kind_icon(hedgr::model::domain::window_kind value) noexcept;

/**
 * @brief The icon naming a lock state.
 *
 * An open padlock while unlocked, a closed one while locked, and a warning
 * triangle while absent. The triangle is not a padlock because absent is not a
 * lock state to read but a first run that wants an action: an operator who is
 * shown a closed padlock looks for a key they never made.
 */
[[nodiscard]] HEDGR_UI_EXPORT const char*
store_lock_icon(hedgr::model::domain::store_lock value) noexcept;

/**
 * @brief The colour of the lock indicator.
 *
 * Grey while the store is open, because an open lock is a normal condition and
 * not news; amber while locked or absent, because both want an action from the
 * operator; and red only after a refused attempt, which is the one state that
 * describes something that went wrong rather than something that simply is.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
store_lock_colour(hedgr::model::domain::store_lock value) noexcept;

/**
 * @brief The colour of one level of the password strength meter.
 *
 * Red at the bottom two, because neither is a password the deployment will
 * take; amber in the middle, which is a password that has started well and is
 * not there yet; and green at the top two, because both satisfy the policy and
 * what separates them is room to spare rather than acceptability.
 *
 * An out-of-range level is grey, which is the colour of not knowing. It is not
 * red: a level nobody defined is a defect in the caller, and drawing it as a
 * refusal would report it as a fact about the password.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32 strength_colour(int strength) noexcept;

}

#endif
