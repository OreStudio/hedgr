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
#include "imgui_internal.h"
#include "implot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

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
const ImVec4 bg_chip_quiet = with_alpha(colour(0x161b21ff), field_alpha); // ALPHA chip, a step below STRONG; derived
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

/*
 * The chart reference draws its series in a violet and its last price chip in
 * a slightly deeper one. Both are sampled from
 * 3dc3688ce58c780167ddf6310cf6953df12318ce-1600x938.png: the line's most
 * common saturated pixel and the chip fill of the Price plot (x 470 y 272).
 * The rest of the palette stays navy, so these two mark data and nothing else.
 */
const ImVec4 chart_line = colour(0x8068d6ff); // Price line, x 40 to 430 y 80 to 410
const ImVec4 chart_tag = colour(0x7c59e9ff); // Price chip, x 470 y 272

/*
 * The tab strip of the tab reference (docs/samples/572312177-...png), sampled
 * from its Statistics, Viewport and Signal Analyzer tabs (all three the same
 * fill, so a focused strip is not told from the others), its Dear ImGui Demo
 * idle tab, the strip behind them and the labels. The hover is not in the
 * reference, which shows no hovered tab, so it is a step lighter than the
 * chosen fill and derived.
 */
const ImVec4 tab_idle = colour(0x2b2d38ff); // Dear ImGui Demo tab, x 160 to 320 y 200
const ImVec4 tab_hover = colour(0x84719fff); // derived
const ImVec4 tab_active = colour(0x73608dff); // Statistics tab, x 12 to 140 y 60
const ImVec4 tab_bar = colour(0x191923ff); // strip behind the tabs, x 300 y 60
const ImVec4 tab_label = colour(0xcacde0ff); // label glyphs of every tab, chosen or idle

/*
 * The menu panel of the tab reference: a flat fill under the card, an outline,
 * and the chart purple for the chosen row. The reference's menu fill and rows
 * are read by eye off its Grid Size popup, so the fill and outline are marked
 * derived; the purple is the one already sampled above.
 */
const ImVec4 menu_fill = colour(0x0b0e14ff); // derived
const ImVec4 menu_edge = colour(0x262a36ff); // derived
constexpr float menu_hover_alpha = 0.55f; // derived
const ImVec4 menu_active = colour(0x8d6cf0ff); // derived

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
    style.PopupRounding = hedgr::ui::theme::menu_rounding;
    style.GrabRounding = 3.0f;
    style.TabRounding = hedgr::ui::theme::tab_rounding;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 0.0f;
    style.ScrollbarRounding = 6.0f;
    style.DockingSeparatorSize = hedgr::ui::theme::dock_gap;
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
     * A folder tab: rounded top, square bottom, no outline and no overline. The
     * rule under the strip takes the chosen tab's colour in ImGui, which is
     * the reference's violet line. The reference draws a strip that has lost
     * focus the same as a focused one, so the dimmed pair is the plain pair.
     * The overline colour stays the chart purple for the widget tabs, which
     * draw their own underline from it.
     */
    colors[ImGuiCol_Tab] = tab_idle;
    colors[ImGuiCol_TabHovered] = tab_hover;
    colors[ImGuiCol_TabSelected] = tab_active;
    colors[ImGuiCol_TabSelectedOverline] = chart_line;
    colors[ImGuiCol_TabDimmed] = tab_idle;
    colors[ImGuiCol_TabDimmedSelected] = tab_active;
    colors[ImGuiCol_TabDimmedSelectedOverline] = chart_line;
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

ImVec4 accent_colour() noexcept {
    return accent;
}

ImVec4 chart_line_colour() noexcept {
    return chart_line;
}

ImVec4 tab_idle_colour() noexcept {
    return tab_idle;
}

ImVec4 tab_hover_colour() noexcept {
    return tab_hover;
}

ImVec4 tab_active_colour() noexcept {
    return tab_active;
}

ImVec4 tab_bar_colour() noexcept {
    return tab_bar;
}

ImVec4 tab_label_colour() noexcept {
    return tab_label;
}

ImVec4 chart_tag_colour() noexcept {
    return chart_tag;
}

ImVec4 menu_surface() noexcept {
    return ImVec4(menu_fill.x, menu_fill.y, menu_fill.z,
        menu_fill.w * menu_popup_alpha);
}

ImVec4 toolbar_fill() noexcept {
    return menu_surface();
}

ImVec4 toolbar_outline() noexcept {
    return chart_tag;
}

ImVec4 menu_bar_edge() noexcept {
    return ImVec4(menu_edge.x, menu_edge.y, menu_edge.z,
        menu_edge.w * menu_bar_alpha);
}

ImVec4 menu_border() noexcept {
    return menu_edge;
}

ImVec4 status_bar_surface() noexcept {
    return menu_fill;
}

ImVec4 status_bar_edge() noexcept {
    return menu_edge;
}

ImVec4 menu_selected_colour() noexcept {
    return chart_tag;
}

ImVec4 menu_hover_colour() noexcept {
    return with_alpha(chart_tag, menu_hover_alpha);
}

ImVec4 menu_active_colour() noexcept {
    return menu_active;
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
    case tone::quiet:
        break;
    }
    return 0;
}

[[nodiscard]] constexpr ImU32 tint(ImU32 base, unsigned int alpha) noexcept {
    return (base & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

}

tone_colours pill_colours(tone value) noexcept {
    if (value == tone::quiet) {
        return tone_colours{
            .fill = ImGui::ColorConvertFloat4ToU32(bg_chip_quiet),
            .border = ImGui::ColorConvertFloat4ToU32(separator),
            .text = ImGui::ColorConvertFloat4ToU32(text_dim)};
    }
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
            .text = ImGui::ColorConvertFloat4ToU32(
                value == tone::quiet ? text_dim : text)};
    }
    /* The CURRENT P/L strip reads #131E24 on the same card. */
    return tone_colours{.fill = tint(base, 0x12),
        .border = tint(base, 0x40), .text = base};
}

gradient band_fade(tone value) noexcept {
    const ImU32 base = tone_base(value);
    if (base == 0) {
        const ImU32 flat = band_colours(value).fill;
        return gradient{.from = flat, .to = flat};
    }
    return gradient{.from = tint(base, band_fade_strong_alpha),
        .to = tint(base, band_fade_faint_alpha)};
}

namespace {

[[nodiscard]] float coordinate(const ImDrawVert& v, axis direction) noexcept {
    return direction == axis::vertical ? v.pos.y : v.pos.x;
}

[[nodiscard]] unsigned int channel(ImU32 colour, unsigned int shift) noexcept {
    return (colour >> shift) & 0xffu;
}

[[nodiscard]] unsigned int mix(unsigned int a, unsigned int b, float t) noexcept {
    return static_cast<unsigned int>(static_cast<float>(a)
        + (static_cast<float>(b) - static_cast<float>(a)) * t + 0.5f);
}

/*
 * The vertex's own alpha scales the gradient's, because an antialiased fill
 * fades its fringe to nothing and that fringe must stay transparent.
 */
void shade(std::span<ImDrawVert> vertices, float start, float end,
    gradient colours, axis direction) noexcept {
    const float length = end - start;
    for (ImDrawVert& v : vertices) {
        const float t = length > 0.0f
            ? std::clamp((coordinate(v, direction) - start) / length, 0.0f, 1.0f)
            : 0.0f;
        const unsigned int alpha = mix(channel(colours.from, IM_COL32_A_SHIFT),
            channel(colours.to, IM_COL32_A_SHIFT), t);
        v.col = IM_COL32(
            mix(channel(colours.from, IM_COL32_R_SHIFT),
                channel(colours.to, IM_COL32_R_SHIFT), t),
            mix(channel(colours.from, IM_COL32_G_SHIFT),
                channel(colours.to, IM_COL32_G_SHIFT), t),
            mix(channel(colours.from, IM_COL32_B_SHIFT),
                channel(colours.to, IM_COL32_B_SHIFT), t),
            alpha * channel(v.col, IM_COL32_A_SHIFT) / 255u);
    }
}

}

void fill_gradient(ImDrawList* draw, ImVec2 min, ImVec2 max,
    gradient colours, axis direction, float rounding, ImDrawFlags flags) {
    const int first = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, flags);
    const std::span<ImDrawVert> added(draw->VtxBuffer.Data + first,
        static_cast<std::size_t>(draw->VtxBuffer.Size - first));
    if (direction == axis::vertical)
        shade(added, min.y, max.y, colours, direction);
    else
        shade(added, min.x, max.x, colours, direction);
}

ImU32 stops_colour(std::span<const gradient_stop> stops, float t) noexcept {
    if (stops.empty())
        return 0;
    if (t <= stops.front().at)
        return stops.front().colour;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t > stops[i].at)
            continue;
        const gradient_stop& a = stops[i - 1];
        const gradient_stop& b = stops[i];
        const float u = b.at > a.at ? (t - a.at) / (b.at - a.at) : 1.0f;
        const auto lerp = [&](unsigned int shift) {
            return mix(channel(a.colour, shift), channel(b.colour, shift), u);
        };
        return IM_COL32(lerp(IM_COL32_R_SHIFT), lerp(IM_COL32_G_SHIFT),
            lerp(IM_COL32_B_SHIFT), lerp(IM_COL32_A_SHIFT));
    }
    return stops.back().colour;
}

void fill_stops(ImDrawList* draw, ImVec2 min, ImVec2 max,
    std::span<const gradient_stop> stops, float rounding) {
    if (stops.size() < 2 || max.x <= min.x)
        return;
    /* Past the clip by more than an antialiased fringe is wide. */
    constexpr float overlap = 2.0f;
    const float width = max.x - min.x;
    const std::size_t last = stops.size() - 2;
    for (std::size_t i = 0; i <= last; ++i) {
        const float from = i == 0 ? min.x
            : std::round(min.x + stops[i].at * width);
        const float to = i == last ? max.x
            : std::round(min.x + stops[i + 1].at * width);
        const ImVec2 clip_min(i == 0 ? from - overlap : from, min.y - overlap);
        const ImVec2 clip_max(i == last ? to + overlap : to, max.y + overlap);
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (i == 0)
            corners |= ImDrawFlags_RoundCornersLeft;
        if (i == last)
            corners |= ImDrawFlags_RoundCornersRight;
        draw->PushClipRect(clip_min, clip_max, true);
        const int first = draw->VtxBuffer.Size;
        draw->AddRectFilled(
            ImVec2(i == 0 ? min.x : from - overlap, min.y),
            ImVec2(i == last ? max.x : to + overlap, max.y),
            IM_COL32_WHITE, rounding, corners);
        for (int v = first; v < draw->VtxBuffer.Size; ++v) {
            ImDrawVert& vertex = draw->VtxBuffer[v];
            const ImU32 colour = stops_colour(
                stops, (vertex.pos.x - min.x) / width);
            vertex.col = (colour & ~IM_COL32_A_MASK)
                | ((channel(colour, IM_COL32_A_SHIFT)
                       * channel(vertex.col, IM_COL32_A_SHIFT) / 255u)
                    << IM_COL32_A_SHIFT);
        }
        draw->PopClipRect();
    }
}

namespace {

struct tab_fade final {
    ImU32 bottom;
    ImU32 top;
};

}

void shade_tab(ImDrawList* draw, int first, ImVec2 min, ImVec2 max) {
    const std::array<tab_fade, 3> fades{{
        {ImGui::ColorConvertFloat4ToU32(tab_idle), tab_idle_top},
        {ImGui::ColorConvertFloat4ToU32(tab_hover), tab_hover_top},
        {ImGui::ColorConvertFloat4ToU32(tab_active), tab_active_top}}};
    /*
     * An antialiased fill keeps its solid vertices half a pixel inside its
     * shape, so the fade runs between those, which puts the lighter stop on
     * the top edge and the sampled fill on the bottom one.
     */
    constexpr float inset = 0.5f;
    const float height = max.y - min.y - 2.0f * inset;
    if (height <= 0.0f)
        return;
    /* The fringe of an antialiased fill reaches a pixel past its shape. */
    constexpr float fringe = 1.0f;
    for (int i = first; i < draw->VtxBuffer.Size; ++i) {
        ImDrawVert& v = draw->VtxBuffer[i];
        if (v.pos.x < min.x - fringe || v.pos.x > max.x + fringe
            || v.pos.y < min.y - fringe || v.pos.y > max.y + fringe)
            continue;
        for (const tab_fade& fade : fades) {
            if ((v.col & ~IM_COL32_A_MASK) != (fade.bottom & ~IM_COL32_A_MASK))
                continue;
            const float t = std::clamp((v.pos.y - min.y - inset) / height, 0.0f, 1.0f);
            const auto lerp = [&](unsigned int shift) {
                return mix(channel(fade.top, shift),
                    channel(fade.bottom, shift), t);
            };
            v.col = IM_COL32(lerp(IM_COL32_R_SHIFT), lerp(IM_COL32_G_SHIFT),
                lerp(IM_COL32_B_SHIFT), channel(v.col, IM_COL32_A_SHIFT));
            break;
        }
    }
}

void shade_dock_tabs() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    for (const ImGuiStoragePair& pair : g.DockContext.Nodes.Data) {
        const auto* node = static_cast<const ImGuiDockNode*>(pair.val_p);
        if (node == nullptr || node->TabBar == nullptr
            || node->HostWindow == nullptr || !node->IsVisible)
            continue;
        const ImGuiTabBar& bar = *node->TabBar;
        const float foot = bar.BarRect.Max.y - g.Style.TabBarBorderSize;
        for (const ImGuiTabItem& tab : bar.Tabs) {
            const float left = bar.BarRect.Min.x
                + std::trunc(tab.Offset - bar.ScrollingAnim);
            shade_tab(node->HostWindow->DrawList, 0,
                ImVec2(left, bar.BarRect.Min.y),
                ImVec2(left + tab.Width, foot));
        }
    }
}

void paint_dock_cards(ImGuiID dock_space) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImU32 strip = ImGui::ColorConvertFloat4ToU32(tab_bar);
    for (const ImGuiStoragePair& pair : g.DockContext.Nodes.Data) {
        const auto* node = static_cast<const ImGuiDockNode*>(pair.val_p);
        if (node == nullptr || !node->IsLeafNode() || !node->IsVisible
            || node->HostWindow == nullptr || node->Windows.empty()
            || ImGui::DockNodeGetRootNode(const_cast<ImGuiDockNode*>(node))->ID
                != dock_space)
            continue;
        ImDrawList* const draw = node->HostWindow->DrawList;
        const bool has_strip = node->TabBar != nullptr && !node->IsHiddenTabBar();
        const ImVec2 end(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y);
        const float top = has_strip ? node->TabBar->BarRect.Max.y : node->Pos.y;
        /* A host window keeps channel 0 for backgrounds and 1 for its tabs. */
        draw->ChannelsSetCurrent(0);
        if (has_strip)
            draw->AddRectFilled(node->Pos, ImVec2(end.x, top), strip,
                dock_rounding, ImDrawFlags_RoundCornersTop);
        /* The body's own rect carries the window gradient, as a window's fill does. */
        fill_gradient(draw, ImVec2(node->Pos.x, top), end, window_gradient,
            axis::vertical, dock_rounding,
            has_strip ? ImDrawFlags_RoundCornersBottom
                      : ImDrawFlags_RoundCornersAll);
        draw->ChannelsSetCurrent(1);
    }
}

background_mark mark_background() {
    ImDrawList* const list = ImGui::GetWindowDrawList();
    return background_mark{.list = list, .start = list->VtxBuffer.Size};
}

void shade_window_background(gradient colours, background_mark parent) {
    const ImGuiWindow* const window = ImGui::GetCurrentWindowRead();
    const ImU32 background = ImGui::GetColorU32(
        (window->Flags & ImGuiWindowFlags_ChildWindow) != 0
            ? ImGuiCol_ChildBg : ImGuiCol_WindowBg);
    ImDrawList* list = window->DrawList;
    int start = 0;
    if (list->VtxBuffer.Size == 0 && parent.list != nullptr) {
        list = parent.list;
        start = parent.start;
    }
    const std::span<ImDrawVert> after(list->VtxBuffer.Data + start,
        static_cast<std::size_t>(list->VtxBuffer.Size - start));
    const auto run = std::ranges::find_if(after, [&](const ImDrawVert& v) {
        return (v.col & ~IM_COL32_A_MASK) != (background & ~IM_COL32_A_MASK);
    });
    const std::span<ImDrawVert> fill(after.begin(), run);
    if (fill.empty())
        return;
    const auto [top, bottom] = std::ranges::minmax(fill, {},
        [](const ImDrawVert& v) { return v.pos.y; });
    shade(fill, top.pos.y, bottom.pos.y, colours, axis::vertical);
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
    case hedgr::model::domain::window_kind::mini_plot:
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
