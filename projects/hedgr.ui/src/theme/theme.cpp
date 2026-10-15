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

// Backgrounds carry alpha: the photographic background is meant to read
// through the panels, which is what makes the desk feel like one surface.
const ImVec4 bg_base = colour(0x0d0d0df0);
const ImVec4 bg_window = colour(0x121212f2);
const ImVec4 bg_title = colour(0x1a1a1aff);
const ImVec4 bg_frame = colour(0x1e1e1eff);
const ImVec4 bg_frame_hover = colour(0x282828ff);
const ImVec4 border = colour(0x2a2a2aff);
const ImVec4 text = colour(0xd4d4d4ff);
const ImVec4 text_dim = colour(0x808080ff);
const ImVec4 accent = colour(0x2d8cffff);
const ImVec4 accent_hover = colour(0x3d9cffff);
const ImVec4 accent_active = colour(0x1f6fd0ff);

}

namespace hedgr::ui::theme {

void apply_theme() {
    ImGui::StyleColorsDark();
    ImPlot::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    /*
     * Geometry: compact padding, square corners, boxed frames. A trading
     * terminal packs rows rather than breathing, and rounded corners on a
     * dense grid read as sloppy rather than friendly.
     */
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.CellPadding = ImVec2(5.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;
    style.SeparatorTextBorderSize = 1.0f;

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_dim;
    colors[ImGuiCol_WindowBg] = bg_window;
    colors[ImGuiCol_ChildBg] = colour(0x00000000);
    colors[ImGuiCol_PopupBg] = colour(0x141414fa);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = colour(0x00000000);
    colors[ImGuiCol_FrameBg] = bg_frame;
    colors[ImGuiCol_FrameBgHovered] = bg_frame_hover;
    colors[ImGuiCol_FrameBgActive] = colour(0x323232ff);
    colors[ImGuiCol_TitleBg] = bg_title;
    colors[ImGuiCol_TitleBgActive] = bg_title;
    colors[ImGuiCol_TitleBgCollapsed] = bg_base;
    colors[ImGuiCol_MenuBarBg] = bg_title;
    colors[ImGuiCol_ScrollbarBg] = colour(0x0d0d0d80);
    colors[ImGuiCol_ScrollbarGrab] = colour(0x333333ff);
    colors[ImGuiCol_ScrollbarGrabHovered] = colour(0x454545ff);
    colors[ImGuiCol_ScrollbarGrabActive] = accent_active;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_active;
    colors[ImGuiCol_Button] = colour(0x242424ff);
    colors[ImGuiCol_ButtonHovered] = colour(0x333333ff);
    colors[ImGuiCol_ButtonActive] = accent_active;
    colors[ImGuiCol_Header] = colour(0x242424ff);
    colors[ImGuiCol_HeaderHovered] = colour(0x333333ff);
    colors[ImGuiCol_HeaderActive] = colour(0x3d3d3dff);
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent_hover;
    colors[ImGuiCol_SeparatorActive] = accent_active;
    colors[ImGuiCol_ResizeGrip] = colour(0x24242480);
    colors[ImGuiCol_ResizeGripHovered] = accent_hover;
    colors[ImGuiCol_ResizeGripActive] = accent_active;
    colors[ImGuiCol_Tab] = bg_title;
    colors[ImGuiCol_TabHovered] = accent_hover;
    colors[ImGuiCol_TabSelected] = accent_active;
    colors[ImGuiCol_TabDimmed] = bg_base;
    colors[ImGuiCol_TabDimmedSelected] = bg_title;
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accent_hover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accent_hover;
    colors[ImGuiCol_TableHeaderBg] = bg_title;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = colour(0x1e1e1eff);
    colors[ImGuiCol_TableRowBg] = colour(0x00000000);
    colors[ImGuiCol_TableRowBgAlt] = colour(0xffffff08);
    colors[ImGuiCol_TextSelectedBg] = colour(0x2d8cff59);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = colour(0xffffff20);
    colors[ImGuiCol_NavWindowingDimBg] = colour(0x00000094);
    colors[ImGuiCol_ModalWindowDimBg] = colour(0x00000094);

    /*
     * ImPlot keeps its own palette, so it is themed here too: a plot drawn in
     * ImPlot's defaults would be the one bright, rounded thing on the desk.
     */
    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.PlotPadding = ImVec2(6.0f, 6.0f);
    plot.LabelPadding = ImVec2(4.0f, 4.0f);
    plot.LegendPadding = ImVec2(4.0f, 4.0f);
    plot.PlotBorderSize = 1;
    plot.MinorAlpha = 0.15f;
    plot.Colors[ImPlotCol_PlotBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
    plot.Colors[ImPlotCol_PlotBorder] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    plot.Colors[ImPlotCol_AxisText] = text;
    plot.Colors[ImPlotCol_AxisGrid] = ImVec4(0.20f, 0.20f, 0.20f, 0.60f);
    plot.Colors[ImPlotCol_AxisTick] = text_dim;

    /*
     * Series colours come from a colormap rather than from the colour array:
     * ImPlot 1.0 keeps plot styling colours and series colours apart, and
     * Dark is the qualitative map whose first entries sit closest to the
     * accent already used for selection.
     */
    plot.Colormap = ImPlotColormap_Dark;
}

ImU32 status_colour(hedgr::model::domain::session_status value) noexcept {
    switch (value) {
    case hedgr::model::domain::session_status::disconnected:
        return IM_COL32(128, 128, 128, 255);
    case hedgr::model::domain::session_status::connecting:
        return IM_COL32(255, 193, 7, 255);
    case hedgr::model::domain::session_status::authenticated:
        return IM_COL32(0, 200, 83, 255);
    case hedgr::model::domain::session_status::failed:
        return IM_COL32(255, 23, 68, 255);
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
        return IM_COL32(0, 200, 83, 255);
    case hedgr::model::domain::label_colour::amber:
        return IM_COL32(255, 193, 7, 255);
    case hedgr::model::domain::label_colour::red:
        return IM_COL32(255, 23, 68, 255);
    case hedgr::model::domain::label_colour::purple:
        return IM_COL32(186, 104, 200, 255);
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 text_colour_on(ImU32 background) noexcept {
    const auto channel = [&](unsigned int shift) {
        return static_cast<float>((background >> shift) & 0xFFu);
    };

    /*
     * Rec. 601 weights, because the eye is far more sensitive to green than to
     * blue and a plain mean would call a saturated blue bright. The result is
     * 0..1.
     */
    const float luminance =
        (0.299f * channel(16) + 0.587f * channel(8) + 0.114f * channel(0))
        / 255.0f;

    /*
     * Half of the scale, because that is where the label palette changes over:
     * amber at 0.75, grey at 0.62, blue at 0.56, purple at 0.55 and green at
     * 0.50 are all pale enough to want dark text, and red at 0.38 wants the
     * pale one. The threshold has to sit above green -- 0.4 would give a
     * saturated green dark text, which is the combination a reader finds worst
     * -- and a declared "dark is under half, light is over half" is the reading
     * of the scale that needs no second table to justify it.
     */
    return luminance > 0.5f ? IM_COL32(24, 24, 24, 255)
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
        return IM_COL32(128, 128, 128, 255);
    case hedgr::model::domain::reachability::unreachable:
        return IM_COL32(255, 23, 68, 255);
    case hedgr::model::domain::reachability::reachable:
        return IM_COL32(0, 200, 83, 255);
    }
    return IM_COL32(212, 212, 212, 255);
}

ImU32 readiness_colour(hedgr::model::domain::readiness value) noexcept {
    switch (value) {
    case hedgr::model::domain::readiness::unknown:
        return IM_COL32(128, 128, 128, 255);
    case hedgr::model::domain::readiness::bootstrapping:
        return IM_COL32(255, 193, 7, 255);
    case hedgr::model::domain::readiness::ready:
        return IM_COL32(0, 200, 83, 255);
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
        return IM_COL32(128, 128, 128, 255);

    /*
     * Reachability first, because a broker that did not answer is the whole
     * story: nothing is known about the service behind it.
     */
    if (result.reach == reachability::unreachable)
        return IM_COL32(255, 23, 68, 255);

    /*
     * Reachable and not finished. Both readiness answers that are not ready
     * land here, and the reachability the probe did not fill in lands here with
     * them, because an instance in the catalogue whose probe has reported
     * something has been answered for.
     */
    if (result.ready != readiness::ready)
        return IM_COL32(255, 193, 7, 255);

    return IM_COL32(0, 200, 83, 255);
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
    return result.ready == readiness::ready ? "ready" : "not ready";
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
        return icon::dashboard;
    case hedgr::model::domain::window_kind::accounts:
        return icon::account;
    case hedgr::model::domain::window_kind::workspace:
        return icon::session;
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
        return IM_COL32(255, 193, 7, 255);
    case hedgr::model::domain::store_lock::locked:
        return IM_COL32(255, 193, 7, 255);
    case hedgr::model::domain::store_lock::unlocked:
        return IM_COL32(128, 128, 128, 255);
    case hedgr::model::domain::store_lock::failed:
        return IM_COL32(255, 23, 68, 255);
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
        return IM_COL32(255, 23, 68, 255);
    case 2:
        return IM_COL32(255, 193, 7, 255);
    case 3:
    case 4:
        return IM_COL32(0, 200, 83, 255);
    default:
        return IM_COL32(128, 128, 128, 255);
    }
}

}
