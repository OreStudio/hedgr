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
#include "hedgr.ui/theme/theme.hpp"

#include "hedgr.ui/icon/icons.hpp"
#include "implot.h"

namespace {

/*
 * The palette. Every value is written once here and referred to by name
 * below, so a colour is changed in one place.
 */
constexpr ImVec4 colour(std::uint32_t rgba) {
    return ImVec4(static_cast<float>((rgba >> 24) & 0xffu) / 255.0f,
        static_cast<float>((rgba >> 16) & 0xffu) / 255.0f,
        static_cast<float>((rgba >> 8) & 0xffu) / 255.0f,
        static_cast<float>(rgba & 0xffu) / 255.0f);
}

/*
 * The palette is read off the reference screenshot rather than invented, and
 * each entry names the point it was sampled from so the next person can check
 * the reading instead of trusting it. Points are given as an x and y in the
 * reference's own pixels.
 *
 * The reference is docs/samples/1792564434861.jpeg, a navy signals list. It is
 * a compressed JPEG, so every value is read from a flat interior run several
 * pixels wide rather than from one pixel, and a value marked as derived is a
 * step taken from a sampled neighbour because the reference never draws that
 * state. Every surface is opaque: stacked windows and panels must not read
 * through each other.
 */
constexpr ImVec4 with_alpha(ImVec4 base, float alpha) {
    base.w = alpha;
    return base;
}

constexpr float window_alpha = 1.00f; // the window's own body; opaque so windows do not read through each other
constexpr float panel_alpha = 1.00f; // a child pane and a card upon it, opaque
constexpr float chrome_alpha = 1.00f; // title bar, status foot, buttons, opaque
constexpr float field_alpha = 1.00f; // a text box or a combo, inside a card, opaque

/*
 * The reference's cards are lit from the top: the fill runs from #13181E under
 * the header to #0D1017 at the foot. The card itself takes the middle of that
 * run, the window takes its foot, and the title bar takes its head, so the three
 * steps Hedgr needs are all points on the one gradient the reference draws.
 */
const ImVec4 bg_base = with_alpha(colour(0x060910ff), chrome_alpha); // page between cards, x 100 y 100
const ImVec4 bg_window = with_alpha(colour(0x0d1017ff), window_alpha); // foot of a card's fill, x 240 y 420
const ImVec4 bg_panel = with_alpha(colour(0x0f141aff), panel_alpha); // card interior, x 450 y 300 and y 640
const ImVec4 bg_status = with_alpha(colour(0x0a0e11ff), chrome_alpha); // shadow under a card, x 240 y 429
const ImVec4 bg_title = with_alpha(colour(0x13181eff), chrome_alpha); // head of a card's fill, x 240 y 448
const ImVec4 bg_frame = with_alpha(colour(0x1c2127ff), field_alpha); // STRONG chip, x 176 to 199 y 391
const ImVec4 bg_frame_hover = with_alpha(colour(0x22282fff), field_alpha); // derived
const ImVec4 bg_frame_active = with_alpha(colour(0x282e36ff), field_alpha); // derived
const ImVec4 bg_button = with_alpha(colour(0x1b222cff), chrome_alpha); // DETAILS button, x 384 to 447 y 125
const ImVec4 bg_button_hover = with_alpha(colour(0x232b36ff), chrome_alpha); // derived

/*
 * Two weights of line. The outline of a card is one pixel and is the brighter
 * of the two. The rule that divides a card inside is a step below it, so a
 * card reads as one object with sections rather than as a stack of boxes.
 *
 * The card's top edge reads #292E34 at x 240 y 106. The side edge is used
 * because a one pixel horizontal line loses more of its value to the JPEG's
 * vertical smoothing than a vertical one does.
 */
const ImVec4 border = colour(0x30333aff); // card left edge, x 7 y 640 and y 1000
const ImVec4 separator = colour(0x1f242aff); // rule inside a card, x 300 y 289

/*
 * The scrollbar is built from the theme's own surfaces so it reads as part of
 * the pane it crosses. The groove is the panel taken down a step, the grab is
 * the card border held solid, and the hover lifts that grey by a step. The
 * press takes the accent, which is what every other dragged control does.
 */
const ImVec4 scrollbar_track = with_alpha(colour(0x0a0d12ff), panel_alpha); // derived
const ImVec4 scrollbar_grab = border;
const ImVec4 scrollbar_grab_hover = colour(0x454a52ff); // derived

/*
 * What a reader is meant to read, and the caption beneath it. The reference
 * sets its values near white and its labels in a grey well below them, and the
 * label grey agrees across DATE, CATEGORY and CURRENT P/L. The third weight is
 * derived: the reference draws no disabled text.
 */
const ImVec4 text = colour(0xe6e9eeff); // Tracked tab label, x 318 to 378 y 41 to 61
const ImVec4 text_dim = colour(0x8d9296ff); // DATE label, x 33 to 69 y 302 to 314
const ImVec4 text_disabled = colour(0x555a61ff); // derived

/*
 * The chosen row is a navy wash, the fill the reference gives a symbol chip
 * that is the subject of its card.
 */
const ImVec4 selection_fill = colour(0x1d3247ff); // BTC chip, x 64 to 79 y 125

/*
 * The accent is the reference's blue, which it uses for the place a reader is
 * (the chosen tab's underline) and for the thing a reader can act on (the
 * DETAILS label, #77A6DB). A chosen control, a ticked box and a dragged slider
 * are the same statement. It is blue and not green because green is a state:
 * a chosen row drawn in the live colour would claim a health nobody measured.
 * Nothing is hovered in a still image, so the two neighbouring steps are the
 * accent moved lighter and darker rather than sampled.
 */
const ImVec4 accent = colour(0x729edbff); // Tracked tab underline, x 370 y 87
const ImVec4 accent_hover = colour(0x8db4e6ff); // derived
const ImVec4 accent_active = colour(0x5a88c4ff); // derived

/* The glyph in a bookmark chip, brightest eighth of x 22 to 46 y 122 to 146. */
constexpr ImU32 glow = IM_COL32(197, 224, 252, 255);

/*
 * The second red. A reachable broker with no IAM service behind it is a fault,
 * but it is not the same fault as a broker that never answered, and the estate
 * chart carries no legend, so two lines in one red would be two lines a reader
 * cannot tell apart. The service that is missing gets the darker red.
 *
 * Neither reference draws this fault, so it keeps the value it already had.
 */
constexpr ImU32 service_missing_colour = IM_COL32(211, 47, 47, 255);

/*
 * The state colours, named for the meaning rather than for the hue. A probe
 * that is ready and a session that is authenticated are the same green, and
 * bootstrapping is the same amber, so a reader carries one vocabulary across
 * every window.
 *
 * The values were sampled from the earlier, green reference, off a solid status
 * dot and a solid warning fill. The navy reference draws the same meanings, its
 * gain green (#4FC68B, +0.59% at x 243 y 241), its SHORT red (#D0878D, x 330
 * y 1342) and its tier amber (#D8B882, PRO at x 224 y 44), but only as thin
 * antialiased text in a compressed JPEG. That reads every one of them duller
 * than the colour behind it, so none is a better reading of the same meaning
 * and the state colours keep their values. They stay off the accent on
 * purpose: the accent is blue, so a state can never be mistaken for a choice.
 *
 * Grey is not a shade of either answer. It is the colour of a question nobody
 * has asked yet, and the reference never has to draw one, so it keeps the
 * neutral value it already had rather than borrowing the reference's dimmed
 * text grey, which means something else.
 *
 * The green and the amber are in the header, beside the red, because the
 * tone helpers below hand all three to widgets that are not about any one
 * model state.
 */
constexpr ImU32 unknown_colour = IM_COL32(128, 128, 128, 255);

}

namespace hedgr::ui::theme {

void apply_theme() {
    ImGui::StyleColorsDark();
    ImPlot::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    /*
     * Geometry. The reference is roomier than this interface has been. A
     * navigation row is about twice the text height rather than a hair over
     * it, a card holds its contents well clear of its own edge, and every
     * corner is rounded by a few pixels. The corner radius is small on purpose,
     * because a large one on a one pixel border reads as a bubble rather than
     * as a panel. A card and a window take the reference's card corner, about
     * eight pixels (the outline at x 7 y 106 turns over eight to nine pixels),
     * and a field keeps four, the corner of its chips.
     *
     * FrameBorderSize is set because the reference frames its fields. A field
     * is a filled well inside the card it sits on, and without the outline the
     * well and the card merge into one surface.
     */
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;
    style.SeparatorTextBorderSize = 1.0f;

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_disabled;
    colors[ImGuiCol_WindowBg] = bg_window;
    colors[ImGuiCol_ChildBg] = bg_panel;
    colors[ImGuiCol_PopupBg] = bg_panel;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = colour(0x00000000);
    colors[ImGuiCol_FrameBg] = bg_frame;
    colors[ImGuiCol_FrameBgHovered] = bg_frame_hover;
    colors[ImGuiCol_FrameBgActive] = bg_frame_active;
    colors[ImGuiCol_TitleBg] = bg_title;
    colors[ImGuiCol_TitleBgActive] = bg_title;
    colors[ImGuiCol_TitleBgCollapsed] = bg_base;
    colors[ImGuiCol_MenuBarBg] = bg_title;
    colors[ImGuiCol_ScrollbarBg] = scrollbar_track;
    colors[ImGuiCol_ScrollbarGrab] = scrollbar_grab;
    colors[ImGuiCol_ScrollbarGrabHovered] = scrollbar_grab_hover;
    colors[ImGuiCol_ScrollbarGrabActive] = accent_active;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_active;
    colors[ImGuiCol_Button] = bg_button;
    colors[ImGuiCol_ButtonHovered] = bg_button_hover;
    colors[ImGuiCol_ButtonActive] = bg_frame_active;
    /*
     * A selected row takes the reference's navy wash. Header is the one
     * colour ImGui draws for "this is the chosen one" in a list or a tree, so
     * it is where the reference's selection background lands.
     *
     * The hover and the press are neutral steps, not steps of the same navy.
     * ImGui reaches for HeaderHovered whether or not the row is selected, so a
     * navy hover would have painted the same wash under a row the pointer
     * merely crossed as under the chosen row, and the reference keeps the two
     * states apart. A surface that wants the wash to survive a hover of the
     * chosen row pushes it back for that row alone, which is what the browser
     * does.
     */
    colors[ImGuiCol_Header] = selection_fill;
    colors[ImGuiCol_HeaderHovered] = colour(0x192028ff); // derived
    colors[ImGuiCol_HeaderActive] = colour(0x202833ff); // derived
    colors[ImGuiCol_Separator] = separator;
    colors[ImGuiCol_SeparatorHovered] = accent_hover;
    colors[ImGuiCol_SeparatorActive] = accent_active;
    colors[ImGuiCol_ResizeGrip] = with_alpha(border, 0.5f);
    colors[ImGuiCol_ResizeGripHovered] = accent_hover;
    colors[ImGuiCol_ResizeGripActive] = accent_active;
    /*
     * A selected tab is a raised surface with a thin blue rule, which is how
     * the reference marks its chosen tab, not a block of the accent. A tab is
     * a place, and a tab strip painted solid would outshout the content.
     */
    colors[ImGuiCol_Tab] = bg_base;
    colors[ImGuiCol_TabHovered] = bg_title;
    colors[ImGuiCol_TabSelected] = bg_panel;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = bg_base;
    colors[ImGuiCol_TabDimmedSelected] = bg_title;
    colors[ImGuiCol_TabDimmedSelectedOverline] = border;
    /*
     * The menu bar is a strip of its own above the tabs, in the title bar's
     * surface, so the two navigation levels read as two levels rather than as
     * one band of text.
     */
    colors[ImGuiCol_MenuBarBg] = bg_title;
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accent_hover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accent_hover;
    colors[ImGuiCol_TableHeaderBg] = bg_title;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = separator;
    colors[ImGuiCol_TableRowBg] = colour(0x00000000);
    colors[ImGuiCol_TableRowBgAlt] = colour(0xffffff05);
    colors[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.35f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = colour(0xffffff20);
    colors[ImGuiCol_NavWindowingDimBg] = colour(0x00000094);
    colors[ImGuiCol_ModalWindowDimBg] = colour(0x00000094);

    /*
     * ImPlot keeps its own palette, so it is themed here too: a plot drawn in
     * ImPlot's defaults would be the one bright, rounded thing on the desk.
     *
     * A plot is an inset well rather than a card, so it takes the window's
     * colour and not the panel's, and its frame is the same one pixel border.
     * The axis text is the dim weight, because an axis is a caption on the
     * data rather than data a reader is following.
     */
    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.PlotPadding = ImVec2(10.0f, 10.0f);
    plot.LabelPadding = ImVec2(6.0f, 6.0f);
    plot.LegendPadding = ImVec2(6.0f, 6.0f);
    plot.PlotBorderSize = 1;
    plot.MinorAlpha = 0.15f;
    plot.Colors[ImPlotCol_PlotBg] = bg_window;
    plot.Colors[ImPlotCol_PlotBorder] = border;
    plot.Colors[ImPlotCol_AxisText] = text_dim;
    plot.Colors[ImPlotCol_AxisGrid] = with_alpha(border, 0.4f);
    plot.Colors[ImPlotCol_AxisTick] = text_disabled;

    /*
     * Series colours come from a colormap rather than from the colour array:
     * ImPlot 1.0 keeps plot styling colours and series colours apart, and
     * Deep is the qualitative map whose first entry is a muted blue beside the
     * accent. Every series Hedgr draws today names its own colour, so this is
     * only the fallback for one that does not.
     */
    plot.Colormap = ImPlotColormap_Deep;
}

ImVec4 base_surface() noexcept {
    return bg_base;
}

ImVec4 status_surface() noexcept {
    return bg_status;
}

ImVec4 card_surface() noexcept {
    return bg_panel;
}

ImVec4 card_border() noexcept {
    return border;
}

ImVec4 dim_text() noexcept {
    return text_dim;
}

ImU32 card_title_icon_colour() noexcept {
    /*
     * The reference's pale blue glow, the colour of the glyph in a card's
     * bookmark and chart chips. The icon marks the field and the words only
     * name it. It was the live green, which made a card about an unreachable
     * instance carry the colour of a healthy one, so it now takes no state
     * colour at all.
     */
    return glow;
}

namespace {

/** @brief The semantic colour behind a verdict, or zero for neutral. */
[[nodiscard]] constexpr ImU32 tone_base(tone value) noexcept {
    switch (value) {
    case tone::good:
        return good_colour;
    case tone::bad:
        return failure_colour;
    case tone::warning:
        return warning_colour;
    case tone::neutral:
        break;
    }
    return 0;
}

[[nodiscard]] constexpr ImU32 tint(ImU32 base, unsigned int alpha) noexcept {
    return (base & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

}

tone_colours pill_colours(tone value) noexcept {
    const ImU32 base = tone_base(value);
    if (base == 0) {
        return tone_colours{.fill = ImGui::ColorConvertFloat4ToU32(bg_frame),
            .border = ImGui::ColorConvertFloat4ToU32(border),
            .text = ImGui::ColorConvertFloat4ToU32(text)};
    }
    /*
     * The TARGETING pill reads #122B27 inside and #2B5146 at its edge on a
     * #12171D card, which is the green at about a seventh and at a little
     * under half.
     */
    return tone_colours{.fill = tint(base, 0x24),
        .border = tint(base, 0x73), .text = base};
}

tone_colours band_colours(tone value) noexcept {
    const ImU32 base = tone_base(value);
    if (base == 0) {
        return tone_colours{.fill = ImGui::ColorConvertFloat4ToU32(bg_title),
            .border = ImGui::ColorConvertFloat4ToU32(separator),
            .text = ImGui::ColorConvertFloat4ToU32(text)};
    }
    /* The CURRENT P/L strip reads #131E24 on the same card. */
    return tone_colours{.fill = tint(base, 0x12),
        .border = tint(base, 0x40), .text = base};
}

ImU32 status_colour(hedgr::model::domain::session_status value) noexcept {
    switch (value) {
    case hedgr::model::domain::session_status::disconnected:
        return unknown_colour;
    case hedgr::model::domain::session_status::connecting:
        return warning_colour;
    case hedgr::model::domain::session_status::authenticated:
        return good_colour;
    case hedgr::model::domain::session_status::failed:
        return failure_colour;
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 label_colour(hedgr::model::domain::label_colour value) noexcept {
    switch (value) {
    case hedgr::model::domain::label_colour::grey:
        return IM_COL32(158, 158, 158, 255);
    case hedgr::model::domain::label_colour::blue:
        return IM_COL32(64, 160, 255, 255);
    case hedgr::model::domain::label_colour::green:
        return good_colour;
    case hedgr::model::domain::label_colour::amber:
        return warning_colour;
    case hedgr::model::domain::label_colour::red:
        return failure_colour;
    case hedgr::model::domain::label_colour::purple:
        return IM_COL32(186, 104, 200, 255);
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 text_colour_on(ImU32 background) noexcept {
    /*
     * Shift 0 is red, 8 is green and 16 is blue, because IM_COL32 packs red
     * into the low byte. The weights below were always right and the channels
     * they were applied to were not, which happened to agree for every colour
     * in the palette and would not have agreed for the next one added.
     */
    const auto channel = [&](unsigned int shift) {
        return static_cast<float>((background >> shift) & 0xFFu);
    };

    /*
     * Rec. 601 weights, because the eye is far more sensitive to green than to
     * blue and a plain mean would call a saturated blue bright. The result is
     * 0..1.
     */
    const float luminance =
        (0.299f * channel(0) + 0.587f * channel(8) + 0.114f * channel(16))
        / 255.0f;

    /*
     * Below the middle of the scale, because the palette does not split at the
     * middle. Every colour a label is painted with is pale enough for dark text
     * except red: amber at 0.75, grey at 0.62, blue at 0.56, purple at 0.55 and
     * green at 0.50 all sit above the line, and red at 0.38 below it. A
     * threshold of one half would put green, at 0.50, on the pale side, which
     * is not where the palette wants it.
     *
     * So the line is set below green with room to spare rather than exactly at
     * it. An entry that moved across it should show up as a failing test and
     * not as a label nobody can read, and green is the entry with the least
     * room.
     */
    return luminance > 0.45f ? IM_COL32(24, 24, 24, 255)
                             : IM_COL32(250, 250, 250, 255);
}

const char* status_icon(hedgr::model::domain::session_status value) noexcept {
    switch (value) {
    case hedgr::model::domain::session_status::disconnected:
        return icon::disconnect;
    case hedgr::model::domain::session_status::connecting:
        return icon::session_pending;
    case hedgr::model::domain::session_status::authenticated:
        return icon::session;
    case hedgr::model::domain::session_status::failed:
        return icon::session_failed;
    }
    return icon::information;
}

ImU32 reachability_colour(hedgr::model::domain::reachability value) noexcept {
    switch (value) {
    case hedgr::model::domain::reachability::unknown:
        return unknown_colour;
    case hedgr::model::domain::reachability::unreachable:
        return failure_colour;
    case hedgr::model::domain::reachability::reachable:
        return good_colour;
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 readiness_colour(hedgr::model::domain::readiness value) noexcept {
    switch (value) {
    case hedgr::model::domain::readiness::unknown:
        return unknown_colour;
    case hedgr::model::domain::readiness::service_missing:
        return service_missing_colour;
    case hedgr::model::domain::readiness::bootstrapping:
        return warning_colour;
    case hedgr::model::domain::readiness::ready:
        return good_colour;
    }
    return IM_COL32(212, 212, 212, 255);
}

/** @brief The colour of an instance's icon, from what the last probe found. */
ImU32 probe_state_colour(
    const hedgr::model::domain::probe_result& result, bool observed) noexcept {
    using hedgr::model::domain::reachability;
    using hedgr::model::domain::readiness;

    /*
     * No probe has reported, so the row's colour is the question rather than an
     * answer: grey. A deployment that has never answered is not a deployment
     * that refused, and reporting the second is a claim nobody made.
     */
    if (!observed)
        return unknown_colour;

    /*
     * Reachability first, because a broker that did not answer is the whole
     * story: nothing is known about the service behind it.
     */
    if (result.reach == reachability::unreachable)
        return failure_colour;

    /*
     * The broker answered and IAM did not, which is the second fault and gets
     * its own red rather than the first fault's. A deployment that still needs
     * bootstrapping answers this read, so an unanswered read is a service that
     * is not there.
     */
    if (result.ready == readiness::service_missing)
        return service_missing_colour;

    /*
     * Reachable and unfinished. Bootstrapping lands here, and so does the
     * readiness a probe did not fill in, because an instance in the catalogue
     * whose probe has reported something has been answered for.
     */
    if (result.ready != readiness::ready)
        return warning_colour;

    return good_colour;
}

/** @brief The short word for that colour, for a row's tooltip. */
const char* probe_state_word(
    const hedgr::model::domain::probe_result& result, bool observed) noexcept {
    using hedgr::model::domain::reachability;
    using hedgr::model::domain::readiness;

    if (!observed)
        return "unknown";
    if (result.reach == reachability::unreachable)
        return "unreachable";

    /*
     * One word per state, because the two faults and the two healthy states
     * are four answers rather than two, and a reader who hovers over a row is
     * asking which one it is.
     */
    switch (result.ready) {
    case readiness::service_missing:
        return "service missing";
    case readiness::bootstrapping:
        return "bootstrapping";
    case readiness::ready:
        return "ready";
    case readiness::unknown:
        return "unknown";
    }
    return "unknown";
}

const char* window_kind_icon(hedgr::model::domain::window_kind value) noexcept {
    switch (value) {
    case hedgr::model::domain::window_kind::chart:
        return icon::chart;
    case hedgr::model::domain::window_kind::grid:
        return icon::grid;
    case hedgr::model::domain::window_kind::notice:
        return icon::notice;
    case hedgr::model::domain::window_kind::instance_manager:
        return icon::instance;
    case hedgr::model::domain::window_kind::accounts:
        return icon::account;
    case hedgr::model::domain::window_kind::workspace:
        return icon::session;
    case hedgr::model::domain::window_kind::command_centre:
        return icon::dashboard;
    case hedgr::model::domain::window_kind::series_browser:
        return icon::grid;
    case hedgr::model::domain::window_kind::term_structure:
    case hedgr::model::domain::window_kind::crm_matrix:
        return icon::chart;
    }
    return icon::information;
}

const char* store_lock_icon(hedgr::model::domain::store_lock value) noexcept {
    switch (value) {
    case hedgr::model::domain::store_lock::absent:
        return icon::lock_absent;
    case hedgr::model::domain::store_lock::locked:
        return icon::lock_closed;
    case hedgr::model::domain::store_lock::unlocked:
        return icon::lock_open;
    case hedgr::model::domain::store_lock::failed:
        return icon::lock_closed;
    }
    return icon::information;
}

ImU32 store_lock_colour(hedgr::model::domain::store_lock value) noexcept {
    switch (value) {
    case hedgr::model::domain::store_lock::absent:
        return warning_colour;
    case hedgr::model::domain::store_lock::locked:
        return warning_colour;
    case hedgr::model::domain::store_lock::unlocked:
        return unknown_colour;
    case hedgr::model::domain::store_lock::failed:
        return failure_colour;
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 strength_colour(int strength) noexcept {
    /*
     * The label palette's own red, amber and green, so a strength meter and a
     * severity pill read as one visual language rather than two.
     */
    switch (strength) {
    case 0:
    case 1:
        return failure_colour;
    case 2:
        return warning_colour;
    case 3:
    case 4:
        return good_colour;
    default:
        return unknown_colour;
    }
}

}
