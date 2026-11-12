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
#include "hedgr.ui/render/party_foot.hpp"
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
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
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
        widget::draw_view_state_body(status, foot,
            {widget::view_state::error, widget::view_words::read_failed,
                "This window does not hold a term structure."});
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const auto session = signed_in_session(input, handle);
    if (!session) {
        draw_session_state(input, handle, status, foot);
        ImGui::PopID();
        return;
    }
    const auto* row = [&]() -> const hedgr::model::state::curve_row* {
        const auto found = input.curves.find(
            input.sessions.instance_id(*session),
            input.sessions.account_id(*session), terms->curve_key);
        return found ? input.curves.get(*found) : nullptr;
    }();
    const hedgr::model::state::curve_axis* axis = nullptr;
    std::size_t pillars = 0;
    bool has_data = false;
    /*
     * A row that has never been applied names no axis: a read in flight or one
     * that failed leaves the last good curve, and a curve with nothing behind
     * it has no pillars to draw.
     */
    if (row != nullptr && row->axis < input.curves.axes()) {
        axis = &input.curves.axis(row->axis);
        pillars = axis->pillars.size();
        has_data = pillars > 0 && !row->as_of_ns.empty()
            && row->values.size() == row->as_of_ns.size() * pillars;
    }

    const auto history = std::clamp(state.market_view.term_history,
        term_history_min, term_history_max);
    int history_index = history - 1;
    const int primary = has_data
        ? static_cast<int>(row->as_of_ns.size()) - 1 : -1;
    const int first = primary >= 0 ? std::max(0, primary - history + 1) : 0;
    const int count = primary - first + 1;
    const std::int64_t newest_at_ns = primary >= 0 ? row->as_of_ns[
        static_cast<std::size_t>(primary)] : 0;

    /*
     * A failed read that leaves older curves still draws them, and the foot
     * says ERROR over them. The mark takes the plot's place only when there is
     * no curve to draw.
     */
    const bool reading = row != nullptr
        && row->state == hedgr::model::state::curve_state::reading;
    auto facts = session_view_facts(input, handle);
    facts.reading = row == nullptr || reading;
    facts.read_failure = row != nullptr ? std::string_view{row->error}
                                        : std::string_view{};
    facts.has_data = has_data;
    facts.empty_what = "This curve holds no points.";
    const auto verdict = widget::view_state_of(facts);
    trace_view_state("term structure", terms->label, state, verdict);

    set_health_foot(foot,
        hedgr::model::domain::assess_data_health(newest_at_ns,
            input.wall_now_ns, hedgr::model::domain::ir_health_thresholds),
        newest_at_ns, input.wall_now_ns, reading);
    widget::set_view_foot(foot, verdict);
    foot.controls.push_back(widget::foot_combo{
        .id = "history",
        .tooltip = "How many recent curves to overlay",
        .options = term_history_labels.data(),
        .option_count = static_cast<int>(term_history_labels.size()),
        .selected = &history_index});

    ImGui::AlignTextToFramePadding();
    widget::value_pill(terms->label);
    ImGui::SetItemTooltip("%s", terms->curve_key.c_str());
    if (has_data) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Terms", "Tenors on the newest curve.");
        ImGui::SameLine();
        ImGui::Text("%zu", pillars);
    }
    ImGui::SameLine();
    draw_series_flags(input, *session, terms->label);

    const float foot_height = widget::status_foot_height(status, foot);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(
        available.y - foot_height - ImGui::GetStyle().ItemSpacing.y, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##curve-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (!has_data) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 room = ImGui::GetContentRegionAvail();
            widget::draw_view_state(verdict, at,
                ImVec2(at.x + room.x, at.y + room.y),
                theme::view_state_glyph_size);
        } else {
            const auto& xs = axis->positions;
            const auto per_instant = static_cast<std::size_t>(pillars);

            double low = row->values[static_cast<std::size_t>(primary)
                * per_instant];
            double high = low;
            for (int c = first; c <= primary; ++c) {
                const auto* ys = row->values.data()
                    + static_cast<std::size_t>(c) * per_instant;
                for (std::size_t i = 0; i < per_instant; ++i) {
                    if (!std::isfinite(ys[i]))
                        continue;
                    low = std::min(low, ys[i]);
                    high = std::max(high, ys[i]);
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
                /*
                 * One tick per pillar, named by the shared axis: a curve is
                 * read by its tenors, so the axis says every tenor rather than
                 * whichever round numbers the plot would pick.
                 */
                ImPlot::SetupAxisTicks(ImAxis_X1, axis->positions.data(),
                    static_cast<int>(pillars), axis->labels.data());
                ImPlot::SetupAxisLimits(ImAxis_X1, -0.75,
                    static_cast<double>(pillars) - 0.25, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, low - pad, high + pad,
                    ImGuiCond_Always);
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");

                const ImVec4 newest_colour =
                    ImGui::ColorConvertU32ToFloat4(theme::curve_latest_colour);
                for (int c = first; c <= primary; ++c) {
                    const bool newest = c == primary;
                    const auto* ys = row->values.data()
                        + static_cast<std::size_t>(c) * per_instant;

                    if (newest) {
                        fill_gradient_under_curve(xs.data(), ys,
                            static_cast<int>(per_instant),
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
                    ImPlot::PlotLine("##curve", xs.data(), ys,
                        static_cast<int>(per_instant), spec);
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

    const bool changed
        = draw_foot_with_party(input, *session, status, foot, state);
    state.market_view.term_history = history_index + 1;
    if (changed)
        input.market_view.term_history = state.market_view.term_history;
    ImGui::PopID();
}

}
