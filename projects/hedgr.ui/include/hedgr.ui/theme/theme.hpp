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
 * @brief Applies the dark style read off the reference screenshot.
 *
 * Call once, after the ImGui context exists. The palette is sampled from
 * docs/samples rather than chosen, and each value in the implementation names
 * the region of that image it was read from. The surfaces are glass: the
 * second reference lets the render behind its panels show through, and the
 * measurement of it and the alphas that follow from it live in the
 * implementation beside the colours they modify.
 */
HEDGR_UI_EXPORT void apply_theme();

/**
 * @brief The surface a status line sits on.
 *
 * The reference runs a strip along the foot of the window that is darker than
 * the panel above it rather than lighter, which is what tells a reader the
 * line is about the window and not part of its contents. ImGui has no style
 * colour of its own for a status foot, so a widget that draws one pushes this
 * over ImGuiCol_ChildBg for the duration and pops it afterwards.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 status_surface() noexcept;

/**
 * @brief The surface a card sits on.
 *
 * The reference's cards are filled a step above the panel they sit on, which is
 * what makes a wall of them read as objects rather than as ruled sections. The
 * value is the card interior sampled from the reference, and it is exported
 * because a card is drawn as its own child and ImGui has no colour slot for
 * "the box inside the pane".
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 card_surface() noexcept;

/**
 * @brief The one pixel outline of a card.
 *
 * A card is a fill and an outline and nothing else. The outline is the whole of
 * the separation the reference uses between one card and the next, so a caller
 * that drew a card without it would be drawing a smudge rather than a box.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 card_border() noexcept;

/**
 * @brief The colour a card's title line is written in.
 *
 * The reference writes a card's title a step below its value: the value is what
 * a reader came for and the title only says what it is. ImGui's disabled text
 * is a step below even this one and is kept for the caption, so the three lines
 * of a card have three weights rather than two.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 dim_text() noexcept;

/**
 * @brief The colour a card's title glyph is drawn in.
 *
 * The reference writes every card title's icon in the connected green while the
 * title's own words are a step below the value, so the icon identifies the
 * field and the words only name it. It is a helper rather than a constant each
 * caller keeps, because the whole wall has to agree on it and a card that
 * reached for an accent of its own would drift from the one beside it.
 *
 * The reference applies this to every card icon and to no other icon. Its
 * toolbar and its status bar are drawn in the ordinary text colour, so the
 * green means "the title of a card" rather than "a glyph".
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32 card_title_icon_colour() noexcept;

/**
 * @brief The palette's red, for anything that reports a failure.
 *
 * Every refusal in the interface is drawn in this colour: a refused unlock, an
 * unreachable instance, a failed sign-in, and the runtime's own error line.
 * It is named and exported rather than reached through one of the helpers
 * below, because a failure is not a fact about a lock or a session and the
 * next surface that reports one should not have to pick a neighbour to borrow
 * a colour from. The helpers agree on this value; this is the value.
 *
 * The reference screenshot shows no red anywhere, so this could not be
 * resampled and keeps the value it had. That is deliberate and not an
 * oversight, because red is the one meaning the reference never has to draw,
 * and borrowing its amber or its green for a refusal would make the two the
 * same statement.
 */
inline constexpr ImU32 failure_colour = IM_COL32(255, 23, 68, 255);

/*
 * Term-structure history. The newest curve is the accent green the web
 * simulator uses; earlier curves are a grey-blue that fades with age, so a
 * reader can tell the live curve from the ones behind it at a glance.
 */
inline constexpr ImU32 curve_latest_colour = IM_COL32(64, 156, 255, 255);
inline constexpr ImU32 curve_history_colour = IM_COL32(132, 166, 214, 255);

/**
 * @brief The colour of a field whose edited value differs from the stored one.
 *
 * Cyan, chosen for being the one hue in this palette that nothing else claims.
 * The state colours are green, amber and two reds, the labels are blue and
 * purple, and the quiet text is grey, so an unsaved edit borrowing any of them
 * would read as a state, as a label, or as an absence. It was amber, which is
 * what a deployment partway through setting itself up uses, and the two are
 * unrelated: one is a fact about a machine and the other is something the
 * operator has just done.
 *
 * The restyle left this value alone. The reference uses no cyan, so there was
 * nothing to sample, and the argument above is what the colour is for rather
 * than what it looks like, so a new palette does not change it.
 */
inline constexpr ImU32 edit_changed_colour = IM_COL32(0, 188, 212, 255);

/**
 * @brief The colour of a navigation rail's group heading.
 *
 * Read off the reference's "MAIN", "TOOLS" and "SYSTEM" headings, which are set
 * in a green about a third darker than the connected dot rather than in the
 * general text colour. It is a separate entry because a heading is not text a
 * reader follows, it is the name of the set below it, and the reference says so
 * in colour as well as in size.
 */
inline constexpr ImU32 rail_heading_colour = IM_COL32(105, 180, 139, 255);

/**
 * @brief The bright bar down the left edge of a chosen row.
 *
 * The reference marks its chosen navigation row with a bar this bright, a few
 * pixels inside the row's own left edge, and the bar is the part of the cue a
 * reader sees before the wash. It is brighter than @c green on purpose: the
 * wash sits over a panel and darkens the green, so a bar drawn in the dot's own
 * value would read as a second, duller thing beside the selected icon.
 */
inline constexpr ImU32 rail_selection_bar = IM_COL32(124, 241, 171, 255);

/**
 * @brief The outline drawn around a chosen row, in the bar's green.
 *
 * The reference boxes the chosen row as well as washing it. A one pixel rule at
 * this alpha carries the cue without turning the row into a control, and an
 * alpha rather than a solid keeps the box from competing with the bar.
 */
inline constexpr ImU32 rail_selection_border = IM_COL32(124, 241, 171, 150);

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
