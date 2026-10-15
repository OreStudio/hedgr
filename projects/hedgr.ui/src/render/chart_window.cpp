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
#include "hedgr.model/command/command.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "implot.h"
#include <cstddef>
#include <string>
#include <vector>

namespace {

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::state::window_handle;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/**
 * @brief A stand-in series, so the plot is visibly a plot before data arrives.
 *
 * The runtime owns the series; until it delivers one, a window that drew
 * nothing at all would be indistinguishable from a window that is broken.
 */
std::vector<double> placeholder_series() {
    constexpr std::size_t points = 240;
    std::vector<double> values;
    values.reserve(points);
    for (std::size_t i = 0; i < points; ++i) {
        values.push_back(100.0 + 6.0 * (static_cast<double>(i % 32) / 32.0)
            + 2.0 * static_cast<double>(i % 7));
    }
    return values;
}

}

namespace hedgr::ui::render {

/**
 * @brief A chart window: one numeric series against time.
 *
 * The plot is drawn inside a child sized to the window's remaining area, so
 * the plot's own layout is not confused by toolbars or status lines that
 * arrive later.
 */
void render_chart(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status) {
    using hedgr::model::domain::chart_state;

    const auto& chart = std::get<chart_state>(input.windows.state(handle));

    const bool refresh_clicked = ImGui::Button(hedgr::ui::icon::refresh);
    ImGui::SetItemTooltip("Refresh now");
    if (refresh_clicked) {
        command request;
        request.kind = command_kind::refresh_window;
        request.window_id = std::string{input.windows.id(handle)};
        input.commands.push(std::move(request));
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(chart.series_subject.empty()
            ? "(no subject)"
            : chart.series_subject.c_str());

    ImGui::SameLine();
    ImGui::Checkbox("Stats", &state.show_detail);
    ImGui::SetItemTooltip("Show the series summary alongside the plot");

    /*
     * There is no series yet: say so where the plot would be, rather than
     * drawing an empty axis a reader would take for a flat line at zero.
     */
    if (chart.series_subject.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("Waiting for data.");
        ImGui::PopStyleColor();
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild("##plot-area", available, ImGuiChildFlags_Borders)) {
        const auto values = placeholder_series();
        std::vector<double> xs(values.size());
        for (std::size_t i = 0; i < xs.size(); ++i)
            xs[i] = static_cast<double>(i);

        constexpr ImPlotFlags plot_flags = ImPlotFlags_None;
        constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_AutoFit;
        constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_AutoFit;

        if (ImPlot::BeginPlot("##series", ImVec2(-1.0f, -1.0f), plot_flags)) {
            ImPlot::SetupAxes("t", "value", x_flags, y_flags);
            ImPlot::PlotLine("series", xs.data(), values.data(),
                static_cast<int>(values.size()));
            ImPlot::EndPlot();
        }

        /*
         * The summary reads the same buffer the plot just drew, so it cannot
         * disagree with the picture beside it once real data replaces the
         * placeholder.
         */
        if (state.show_detail) {
            double lowest = values.front();
            double highest = values.front();
            for (const auto value : values) {
                lowest = value < lowest ? value : lowest;
                highest = value > highest ? value : highest;
            }
            ImGui::Text("%zu points, last %.2f, min %.2f, max %.2f",
                values.size(), values.back(), lowest, highest);
        }
    }
    ImGui::EndChild();
}

}
