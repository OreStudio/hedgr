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
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/plot_fill.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

}

void render_term_structure(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    using hedgr::model::domain::term_structure_state;

    ImGui::PushID("term-structure");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* terms = std::get_if<term_structure_state>(&shape);
    if (terms == nullptr) {
        ImGui::TextWrapped("%s", "This window is not a term structure.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const auto session = signed_in_session(input, handle);
    if (!session) {
        draw_signed_out(input, status, foot);
        ImGui::PopID();
        return;
    }
    const auto* curve = input.term_structures.get(
        input.sessions.instance_id(*session),
        input.sessions.account_id(*session), terms->curve_key);

    const auto history = std::clamp(state.market_view.term_history,
        term_history_min, term_history_max);
    int history_index = history - 1;
    int first = 0;
    int primary = -1;
    std::int64_t newest_at_ns = 0;
    if (curve != nullptr && !curve->curves.empty()) {
        /*
         * The live stream publishes a few pillars at a time, so the newest
         * timestamp is often a partial curve. The newest complete curve is the
         * one worth drawing boldly; the history window ends there.
         */
        std::size_t best = 0;
        primary = 0;
        for (int c = 0; c < static_cast<int>(curve->curves.size()); ++c) {
            if (curve->curves[static_cast<std::size_t>(c)].nodes.size() >= best) {
                best = curve->curves[static_cast<std::size_t>(c)].nodes.size();
                primary = c;
            }
        }
        first = std::max(0, primary - history + 1);
        if (primary >= 0 && curve->curves[static_cast<std::size_t>(primary)].at_ns > 0)
            newest_at_ns = curve->curves[static_cast<std::size_t>(primary)].at_ns;
    }

    set_health_foot(foot,
        hedgr::model::domain::assess_data_health(newest_at_ns,
            input.wall_now_ns, hedgr::model::domain::ir_health_thresholds),
        newest_at_ns, input.wall_now_ns, curve != nullptr && curve->reading);
    foot.controls.push_back(widget::foot_combo{
        .id = "history",
        .tooltip = "How many recent curves to overlay",
        .options = term_history_labels.data(),
        .option_count = static_cast<int>(term_history_labels.size()),
        .selected = &history_index});

    ImGui::AlignTextToFramePadding();
    widget::value_pill(terms->label);
    ImGui::SetItemTooltip("%s", terms->curve_key.c_str());
    if (primary >= 0 && !curve->curves[static_cast<std::size_t>(primary)].nodes.empty()) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Terms", "Tenors on the newest curve.");
        ImGui::SameLine();
        ImGui::Text("%zu",
            curve->curves[static_cast<std::size_t>(primary)].nodes.size());
    }
    ImGui::SameLine();
    draw_series_flags(input, *session, terms->label);

    if (curve != nullptr && !curve->error.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", curve->error.c_str());
    }

    const float foot_height = widget::status_foot_height(status, foot);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(
        available.y - foot_height - ImGui::GetStyle().ItemSpacing.y, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##curve-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (curve == nullptr
            || (curve->curves.empty() && curve->error.empty())) {
            ImGui::TextDisabled("Reading the curve...");
        } else if (curve->curves.empty()) {
            ImGui::TextWrapped("%s", curve->error.c_str());
        } else {
            const auto& all_curves = curve->curves;
            const int end = primary + 1;
            const int count = end - first;

            const auto& ladder = state.ladder;
            update_term_ladder(state.ladder,
                {.version = curve->version, .history = history}, all_curves,
                static_cast<std::size_t>(first),
                static_cast<std::size_t>(primary));
            const auto& xs = ladder.xs;

            double low = all_curves[static_cast<std::size_t>(primary)].nodes.front().value;
            double high = low;
            for (int c = first; c < end; ++c) {
                for (const auto& node : all_curves[static_cast<std::size_t>(c)].nodes) {
                    low = std::min(low, node.value);
                    high = std::max(high, node.value);
                }
            }
            const double span = high - low > 0.0
                ? high - low
                : std::max(std::abs(high) * 0.005, 1e-6);
            const double pad = span * 0.08;

            constexpr ImPlotFlags plot_flags = ImPlotFlags_Crosshairs
                | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend
                | ImPlotFlags_NoMenus;
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding,
                ImVec2(2.0f, 2.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                ImVec4(0.33f, 0.35f, 0.38f, 0.35f));
            ImPlot::PushStyleColor(ImPlotCol_AxisText,
                ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::PushFont(icon::numeric_font());
            if (ImPlot::BeginPlot("##curve", ImVec2(-1.0f, -1.0f),
                    plot_flags)) {
                ImPlot::SetupAxes("", "", ImPlotAxisFlags_None,
                    ImPlotAxisFlags_Opposite);
                ImPlot::SetupAxisTicks(ImAxis_X1, xs.data(),
                    static_cast<int>(xs.size()), ladder.labels.data());
                ImPlot::SetupAxisLimits(ImAxis_X1, -0.75,
                    static_cast<double>(xs.size()) - 0.25, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, low - pad, high + pad,
                    ImGuiCond_Always);
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");

                const ImVec4 newest_colour =
                    ImGui::ColorConvertU32ToFloat4(theme::curve_latest_colour);
                for (int c = first; c < end; ++c) {
                    const bool newest = c == primary;
                    const auto& ys = ladder.ys[static_cast<std::size_t>(c - first)];

                    if (newest) {
                        fill_gradient_under_curve(xs.data(), ys.data(),
                            static_cast<int>(xs.size()),
                            theme::curve_latest_colour, 0.30f);
                    }

                    const int age_index = c - first;
                    ImVec4 colour = newest_colour;
                    if (!newest) {
                        /*
                         * Every historical curve is a distinct shade of blue:
                         * the oldest is the darkest, and each step towards the
                         * newest is lighter. Fading alpha alone made two
                         * neighbouring curves read as one.
                         */
                        const float age = count > 1
                            ? static_cast<float>(age_index)
                                / static_cast<float>(count - 1)
                            : 1.0f;
                        float red = 0.0f;
                        float green = 0.0f;
                        float blue = 0.0f;
                        ImGui::ColorConvertHSVtoRGB(0.60f - 0.04f * age, 0.75f,
                            0.34f + 0.44f * age, red, green, blue);
                        colour = ImVec4(red, green, blue, 1.0f);
                    }
                    ImPlotSpec spec;
                    spec.LineColor = colour;
                    spec.LineWeight = newest ? 2.5f : 1.2f;
                    if (newest) {
                        spec.Marker = ImPlotMarker_Circle;
                        spec.MarkerSize = 3.0f;
                    }
                    ImGui::PushID(c);
                    ImPlot::PlotLine("##curve", xs.data(), ys.data(),
                        static_cast<int>(xs.size()), spec);
                    ImGui::PopID();
                }
                ImPlot::EndPlot();
            }
            ImGui::PopFont();
            ImPlot::PopStyleColor(2);
            ImPlot::PopStyleVar(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const bool changed = widget::draw_status_foot(status, input.now_ns, foot);
    state.market_view.term_history = history_index + 1;
    if (changed)
        input.market_view.term_history = state.market_view.term_history;
    ImGui::PopID();
}

}
