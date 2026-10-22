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
#include <cstdint>

namespace hedgr::ui::theme {

/**
 * @brief Applies the dark navy style read off the reference screenshot.
 *
 * Call once, after the ImGui context exists. The palette is sampled from
 * docs/samples/1792564434861.jpeg rather than chosen, and each value in the
 * implementation names the point of that image it was read from. Every
 * surface is opaque.
 */
HEDGR_UI_EXPORT void apply_theme();

/**
 * @brief The ground under every window: the reference's page between cards.
 *
 * The frame is cleared to it and the splash is drawn on it, so the two read
 * the theme rather than each keeping a copy of the value.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 base_surface() noexcept;

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
 * The reference draws the glyph in each card's chips in a pale blue while the
 * title's own words are a step below the value, so the icon identifies the
 * field and the words only name it. It carries no state colour, because a card
 * about a failing instance must not wear the live green.
 *
 * It is a helper rather than a constant each caller keeps, because the whole
 * wall has to agree on it and a card that reached for an accent of its own
 * would drift from the one beside it.
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
 * The navy reference draws red only as thin text on a compressed image, which
 * reads duller than the colour behind it, so this keeps the value it had.
 * Borrowing the amber or the green for a refusal would make the two the same
 * statement.
 */
inline constexpr ImU32 failure_colour = IM_COL32(255, 23, 68, 255);

/** @brief The palette's green, for anything that reports a good outcome. */
inline constexpr ImU32 good_colour = IM_COL32(106, 233, 149, 255);

/**
 * @brief The palette's amber, for anything partway or at risk.
 *
 * The warning fill under the Miss Rate label, 600x24 at x 600 y 1022.
 */
inline constexpr ImU32 warning_colour = IM_COL32(240, 193, 87, 255);

/**
 * @brief The outcome a pill or a strip reports, which picks its colours.
 *
 * Neutral is a value with no verdict, such as a timeframe. Quiet is a value
 * with no verdict that is also secondary, such as an empty count, and is drawn
 * as dim text on a dimmer chip. The other three are the state colours, never
 * the accent: the accent marks a choice, and a verdict drawn in it would read
 * as something the reader picked. Quiet has no state version on purpose: a
 * verdict is never secondary.
 */
enum class tone : std::uint8_t { neutral, quiet, good, bad, warning };

/** @brief The three colours a toned shape is drawn with. */
struct tone_colours final {
    ImU32 fill = 0;
    ImU32 border = 0;
    ImU32 text = 0;
};

/**
 * @brief A pill's colours for @p value: a dark tinted fill, a thin border and
 * bright text in the tone's colour.
 *
 * Neutral takes the chip fill, the card border and the ordinary text. Quiet
 * takes a fill a step below the chip, the rule colour as its edge and the dim
 * text, so it reads as present but not asking to be read.
 */
[[nodiscard]] HEDGR_UI_EXPORT tone_colours pill_colours(tone value) noexcept;

/**
 * @brief A strip's colours for @p value.
 *
 * Fainter than a pill's, because a strip spans the card and a pill sits on
 * top of it in the same tone. The text is the pill's.
 */
[[nodiscard]] HEDGR_UI_EXPORT tone_colours band_colours(tone value) noexcept;

/** @brief Two colours a surface fades between, @c from at its start. */
struct gradient final {
    ImU32 from = 0;
    ImU32 to = 0;
};

/** @brief The direction a gradient runs: top to bottom, or left to right. */
enum class axis : std::uint8_t { vertical, horizontal };

/*
 * The reference lights each panel from above: a card reads #13181E at its
 * head, #0F141A through its middle and #0D1017 at its foot, on a flat #06090E
 * page. Each end below is pushed a few levels past the sample so the fade
 * reads on a monitor rather than only under a colour picker.
 */
inline constexpr ImU32 window_gradient_top = IM_COL32(0x13, 0x18, 0x1f, 255);
inline constexpr ImU32 window_gradient_bottom = IM_COL32(0x0a, 0x0d, 0x12, 255);
inline constexpr ImU32 card_gradient_top = IM_COL32(0x18, 0x1e, 0x26, 255);
inline constexpr ImU32 card_gradient_bottom = IM_COL32(0x0c, 0x0f, 0x15, 255);
inline constexpr gradient window_gradient{
    .from = window_gradient_top, .to = window_gradient_bottom};
inline constexpr gradient card_gradient{
    .from = card_gradient_top, .to = card_gradient_bottom};

/**
 * @brief The alpha a strip's tone fill has at its left and at its right end.
 *
 * The CURRENT P/L strip glows green at its left and fades toward the card.
 */
inline constexpr unsigned int band_fade_strong_alpha = 0x26;
inline constexpr unsigned int band_fade_faint_alpha = 0x05;

/**
 * @brief A strip's fill as a left to right fade of its tone.
 *
 * Neutral has no tone to fade, so both ends are its flat fill.
 */
[[nodiscard]] HEDGR_UI_EXPORT gradient band_fade(tone value) noexcept;

/**
 * @brief Fills the rectangle @p min to @p max with @p colours along
 * @p direction, its corners rounded by @p rounding like AddRectFilled's.
 *
 * The rounded rectangle is drawn first and its vertices are recoloured where
 * they lie, so the corners and their antialiased fringe are ImGui's own.
 */
HEDGR_UI_EXPORT void fill_gradient(ImDrawList* draw, ImVec2 min, ImVec2 max,
    gradient colours, axis direction, float rounding,
    ImDrawFlags flags = ImDrawFlags_None);

/**
 * @brief Where the parent's draw list ended before a child was begun.
 *
 * ImGui draws a child's background into its parent's draw list when it can,
 * to save a draw call, so the child's fill starts here rather than at the
 * front of its own list.
 */
struct background_mark final {
    ImDrawList* list = nullptr;
    int start = 0;
};

/** @brief Marks the current window's draw list, to pass to a child's shading. */
[[nodiscard]] HEDGR_UI_EXPORT background_mark mark_background();

/**
 * @brief Recolours the background ImGui drew for the current window as a
 * top to bottom fade in @p colours.
 *
 * Call right after a Begin or a BeginChild that returned true, passing the
 * mark taken just before a BeginChild. ImGui draws a window's background
 * before its title bar, menu bar, scrollbars and outline, so the fill is the
 * run of vertices in the window's background colour at the front of its own
 * draw list, or at the mark when the child was drawn into its parent's. Only
 * that run is touched, which leaves the decorations above it and every corner
 * exactly as ImGui drew them.
 */
HEDGR_UI_EXPORT void shade_window_background(gradient colours,
    background_mark parent = {});

/** @brief The live data-health badge. */
inline constexpr ImU32 health_live_colour = IM_COL32(106, 233, 149, 255);

/*
 * Term-structure history. The newest curve is the blue the web simulator
 * uses; earlier curves are a grey-blue that fades with age, so a
 * reader can tell the live curve from the ones behind it at a glance.
 */
inline constexpr ImU32 curve_latest_colour = IM_COL32(64, 156, 255, 255);

/** @brief The moving-average overlay, distinct from both price directions. */
inline constexpr ImU32 moving_average_colour = IM_COL32(240, 180, 80, 255);

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
 * The reference's label grey, read off its DATE label at x 33 to 69 y 302 to
 * 314, the same value as the theme's dim text. The reference sets every label
 * that names the set below it small, in capitals and dimmed, so a heading is
 * quieter than the rows it heads. It was a green, which read as a health state
 * on a rail that reports none.
 */
inline constexpr ImU32 rail_heading_colour = IM_COL32(141, 146, 150, 255);

/**
 * @brief The bright bar down the left edge of a chosen row.
 *
 * A bar a few pixels inside the row's own left edge is the part of the cue a
 * reader sees before the wash. It takes the reference's brightest blue, the
 * fill of the count badge on its chosen tab at x 389 y 50, which is brighter
 * than the accent on purpose: the navy wash under it would make a bar in the
 * accent's own value read as a second, duller thing beside the selected icon.
 */
inline constexpr ImU32 rail_selection_bar = IM_COL32(91, 165, 255, 255);

/**
 * @brief The outline drawn around a chosen row, in the bar's blue.
 *
 * The reference boxes the chosen row as well as washing it. A one pixel rule at
 * this alpha carries the cue without turning the row into a control, and an
 * alpha rather than a solid keeps the box from competing with the bar.
 */
inline constexpr ImU32 rail_selection_border = IM_COL32(91, 165, 255, 150);

/**
 * @brief The alpha a currency flag image is drawn at, about 85 percent.
 *
 * A fully opaque flag out-shouts the dark card it sits on. A little
 * translucency lets the card show through so the image sits with the theme.
 */
inline constexpr unsigned int flag_alpha = 217;

/** @brief The gap between a base and a quote flag, close enough to read as one badge. */
inline constexpr float flag_pair_gap = 2.0f;

/** @brief A header's flag height as a fraction of the price pill's, so the flags sit quieter than the pill. */
inline constexpr float flag_height_fraction = 0.75f;

/** @brief The accent, the colour of the chosen and of the line a plot draws. */
[[nodiscard]] HEDGR_UI_EXPORT ImVec4 accent_colour() noexcept;

/**
 * @brief The faint dashed rules across a plot, in the dim text grey at a low
 * alpha, so a rule is a guide and never a line of data.
 */
inline constexpr ImU32 plot_grid_colour = IM_COL32(141, 146, 150, 56);

/** @brief The crosshair a plot draws under the pointer. */
inline constexpr ImU32 plot_crosshair_colour = IM_COL32(141, 146, 150, 128);

/** @brief The length of a grid dash and the gap after it, in pixels. */
inline constexpr float plot_grid_dash = 4.0f;
inline constexpr float plot_grid_gap = 5.0f;

/** @brief The length of a dot on the last price line and the gap after it. */
inline constexpr float plot_price_dot = 1.5f;
inline constexpr float plot_price_gap = 3.0f;

/** @brief The alpha of the dotted line across a plot at the last price. */
inline constexpr float plot_price_line_alpha = 0.85f;

/** @brief The weight of the line a plot draws for a series. */
inline constexpr float plot_line_weight = 1.5f;

/** @brief The room between a plot and its tick labels, so labels breathe. */
inline constexpr float plot_label_padding_x = 12.0f;
inline constexpr float plot_label_padding_y = 8.0f;

/** @brief How many labels the bottom axis shows. */
inline constexpr int plot_x_label_count = 4;

/** @brief The corner radius of a segmented control and of its chosen segment. */
inline constexpr float segment_rounding = 6.0f;

/** @brief The room round the segments inside the group, in pixels. */
inline constexpr float segment_inset = 2.0f;

/** @brief The fill of the chosen segment: a step above the group behind it. */
inline constexpr ImU32 segment_selected_fill = IM_COL32(0x2a, 0x31, 0x3b, 255);

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
