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
 * each entry names the region it was sampled from so the next person can check
 * the reading instead of trusting it. Regions are given as an x and y in the
 * reference's own pixels.
 *
 * The second reference, the glass one at
 * docs/samples/267987113-b1f2b31e-a315-4ec4-90ea-f6f0ff00134b.png, shows
 * surfaces that let the render behind them through. Its Outliner panel was
 * measured against the render behind it by fitting the panel's own colour to
 * the render just outside its left edge, which gave an alpha of about 0.75, so
 * about a quarter of what is behind comes through. The window, the panels and
 * the cards take that reading in steps rather than all at once, because a card
 * sits on a panel and two layers at the same alpha would lose the step between
 * them. The alpha is not one number: the photograph behind is already dimmed by
 * the workspace, and a card holds the smallest text in the window and needs the
 * most of its own colour to stay readable.
 */
constexpr ImVec4 with_alpha(ImVec4 base, float alpha) {
    base.w = alpha;
    return base;
}

constexpr float window_alpha = 1.00f; // the window's own body; opaque so windows do not read through each other
constexpr float panel_alpha = 0.82f; // a child pane and a card upon it
constexpr float chrome_alpha = 0.85f; // title bar, status foot, buttons
constexpr float field_alpha = 0.90f; // a text box or a combo, inside a card

const ImVec4 bg_base = with_alpha(colour(0x101113ff), chrome_alpha); // gap between the two panels, x 406 y 700
const ImVec4 bg_window = with_alpha(colour(0x1b1f20ff), window_alpha); // main panel body, x 1600 y 300
const ImVec4 bg_panel = with_alpha(colour(0x1f2324ff), panel_alpha); // card interior, 60x40 at x 840 y 410
const ImVec4 bg_status = with_alpha(colour(0x1a1b1dff), chrome_alpha); // status bar, x 1600 y 1615
const ImVec4 bg_title = with_alpha(colour(0x292d2eff), chrome_alpha); // title bar, x 395 y 35
const ImVec4 bg_frame = with_alpha(colour(0x2b2f30ff), field_alpha); // toolbar field, 300x40 at x 700 y 115
const ImVec4 bg_frame_hover = with_alpha(colour(0x313637ff), field_alpha);
const ImVec4 bg_frame_active = with_alpha(colour(0x363b3cff), field_alpha);
const ImVec4 bg_button = with_alpha(colour(0x292d2eff), chrome_alpha); // Maps button, 100x40 at x 1170 y 115
const ImVec4 bg_button_hover = with_alpha(colour(0x313637ff), chrome_alpha);

/*
 * Two weights of line. The outline of a card is one pixel and sits barely
 * above the surface under it, and the rule beneath a title is one step
 * brighter, which is the whole of the separation the reference uses.
 */
const ImVec4 border = colour(0x3e4243ff); // card left edge, x 434 y 450
const ImVec4 separator = colour(0x494d4eff); // rule under Runtime Overview, x 1600 y 395

/*
 * The scrollbar. It matters now that a details pane can hold more than fits,
 * and it is built from the restyle's own surfaces rather than from ImGui's
 * defaults so it reads as part of the pane it crosses. The groove is the panel
 * taken down a step, which makes the track a recess in the surface rather than
 * a strip laid on top of it. The grab is the card border held solid, because a
 * bar a reader catches should weigh the same as the outlines around it, and the
 * hover lifts that grey by a step. The press takes the accent, which is what
 * every other dragged control in this interface does when it is held.
 */
const ImVec4 scrollbar_track = with_alpha(colour(0x131516ff), panel_alpha);
const ImVec4 scrollbar_grab = border;
const ImVec4 scrollbar_grab_hover = colour(0x565b5cff);

/*
 * What a reader is meant to read, and the caption beneath it. The third weight
 * below is not a sample. The reference draws no disabled text anywhere, so it
 * is the caption grey taken down by a little under half, which keeps it in the
 * same family and puts it below the caption rather than beside it.
 */
const ImVec4 text = colour(0xcdd2d2ff); // Command Center, 300x30 at x 80 y 420
const ImVec4 text_dim = colour(0x8f9595ff); // since payload start, 250x24 at x 455 y 520
const ImVec4 text_disabled = colour(0x585d5eff);

/*
 * The reference's connected green. It is read off the status dot, which is a
 * solid disc and so reaches its own colour rather than an antialiased
 * approximation of it, and the selection bar down the chosen navigation row
 * reads the same value.
 *
 * The reference sets its group headings and its section subtitles in a second
 * green, about a third darker again. That one is recorded in the restyle's
 * report rather than here, because ImGui has one text colour and no heading
 * colour, so there is no style slot that could carry it and an unused constant
 * would be a palette entry that nothing can reach.
 */
const ImVec4 green = colour(0x6ae995ff); // status dot, 22x22 at x 38 y 1616

/*
 * The chosen navigation row is a dark green wash. The reference draws it over
 * a panel rather than over black, so the two clean readings of it differ and
 * the value here is their middle.
 */
const ImVec4 selection_fill = colour(0x1e402eff); // Telemetry row, x 300 y 1250

/*
 * The accent is the reference's bright green, because a chosen control, a
 * ticked box and a dragged slider are all the same statement as a chosen
 * navigation row. Nothing is hovered in a still image, so the two neighbouring
 * steps are the accent moved lighter and darker rather than sampled.
 */
const ImVec4 accent = green;
const ImVec4 accent_hover = colour(0x86f0acff);
const ImVec4 accent_active = colour(0x4fcb7dff);

/*
 * The second red. A reachable broker with no IAM service behind it is a fault,
 * but it is not the same fault as a broker that never answered, and the estate
 * chart carries no legend, so two lines in one red would be two lines a reader
 * cannot tell apart. The service that is missing gets the darker red.
 *
 * The reference shows no red at all, so unlike the green and the amber this
 * one was not resampled and keeps the value it already had.
 */
constexpr ImU32 service_missing_colour = IM_COL32(211, 47, 47, 255);

/*
 * The state colours, named for the meaning rather than for the hue. A probe
 * that is ready and a session that is authenticated are the same green as the
 * reference's connected dot, and bootstrapping is the same amber as its
 * warning fill, so a reader carries one vocabulary between the reference and
 * this window rather than two.
 *
 * Grey is not a shade of either answer. It is the colour of a question nobody
 * has asked yet, and the reference never has to draw one, so it keeps the
 * neutral value it already had rather than borrowing the reference's dimmed
 * text grey, which means something else.
 */
constexpr ImU32 good_colour = IM_COL32(106, 233, 149, 255);

/* The warning fill under the Miss Rate label, 600x24 at x 600 y 1022. */
constexpr ImU32 warning_colour = IM_COL32(240, 193, 87, 255);

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
     * as a panel.
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
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
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
     * A selected row takes the reference's green wash. Header is the one
     * colour ImGui draws for "this is the chosen one" in a list or a tree, so
     * it is where the reference's selection background lands.
     *
     * The hover and the press are neutral greys, not steps of the same green.
     * ImGui reaches for HeaderHovered whether or not the row is selected, so a
     * green hover would have painted the same wash under a row the pointer
     * merely crossed as under the chosen row, and the reference keeps the two
     * states apart. A surface that wants the green to survive a hover of the
     * chosen row pushes it back for that row alone, which is what the browser
     * does.
     */
    colors[ImGuiCol_Header] = selection_fill;
    colors[ImGuiCol_HeaderHovered] = colour(0x282c2dff);
    colors[ImGuiCol_HeaderActive] = colour(0x323637ff);
    colors[ImGuiCol_Separator] = separator;
    colors[ImGuiCol_SeparatorHovered] = accent_hover;
    colors[ImGuiCol_SeparatorActive] = accent_active;
    colors[ImGuiCol_ResizeGrip] = colour(0x3e424380);
    colors[ImGuiCol_ResizeGripHovered] = accent_hover;
    colors[ImGuiCol_ResizeGripActive] = accent_active;
    /*
     * A selected tab is a raised surface with a green rule under it, not a
     * block of the accent. The accent is the colour of a state a reader looks
     * for; a tab is a place, and a tab strip painted in the state colour turns
     * the whole working set into an alarm.
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
    colors[ImGuiCol_TableBorderLight] = colour(0x2a2e2fff);
    colors[ImGuiCol_TableRowBg] = colour(0x00000000);
    colors[ImGuiCol_TableRowBgAlt] = colour(0xffffff05);
    colors[ImGuiCol_TextSelectedBg] = colour(0x6ae99559);
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
    plot.Colors[ImPlotCol_AxisGrid] = colour(0x3e424366);
    plot.Colors[ImPlotCol_AxisTick] = text_disabled;

    /*
     * Series colours come from a colormap rather than from the colour array:
     * ImPlot 1.0 keeps plot styling colours and series colours apart, and
     * Dark is the qualitative map whose first entries sit closest to the
     * accent already used for selection.
     */
    plot.Colormap = ImPlotColormap_Dark;
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
     * The connected green, the same value the status dot carries. The reference
     * writes every card title's icon in it while the title's own words stay a
     * step below the value, so the icon marks the field and the words only name
     * it. Every card takes it from here rather than reaching for the accent, so
     * a change of mind moves the whole wall together.
     */
    return good_colour;
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
