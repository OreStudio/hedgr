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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "implot.h"
#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::state::window_handle;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

}

namespace {

/**
 * @brief A time tick, from the epoch seconds the plot's X axis carries.
 *
 * This ImPlot has no time axis of its own, so the ticks are formatted here.
 * The shape names the day and the minute, which reads for an intraday series
 * and still separates days for a series that spans years.
 */
int format_time(double value, char* buffer, int size, void* data) {
    const auto* pattern = static_cast<const char*>(data);
    const auto seconds = static_cast<std::time_t>(value);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    const auto written = std::strftime(buffer,
        static_cast<std::size_t>(size), pattern, &utc);
    return static_cast<int>(written);
}

}

namespace hedgr::ui::render {

/**
 * @brief One market series against time.
 *
 * The points are read on a cadence by the runtime and handed over as one
 * snapshot, so the plot is a reading of one moment rather than a series of
 * appends that could interleave with the frame. A failed read keeps the last
 * points and says the read failed: a blank plot would look like a market with
 * no prices rather than like a connection problem.
 */
void render_chart(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    static_cast<void>(state);
    static_cast<void>(status);
    using hedgr::model::domain::chart_state;

    ImGui::PushID("chart");
    draw_identity_bar(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* chart = std::get_if<chart_state>(&shape);
    if (chart == nullptr) {
        ImGui::TextWrapped("%s", "This window is not a chart.");
        ImGui::PopID();
        return;
    }

    if (ImGui::Button(icon::refresh))
        input.commands.push(command{
            .kind = command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    ImGui::SetItemTooltip("Read the series again");

    ImGui::SameLine();
    ImGui::TextUnformatted(chart->label.c_str());

    const auto* snapshot = input.market.points(chart->series_id);
    if (snapshot == nullptr || (!snapshot->is_read()
            && snapshot->failure.empty())) {
        ImGui::TextDisabled("Reading the series...");
        ImGui::PopID();
        return;
    }

    if (!snapshot->failure.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", snapshot->failure.c_str());
    }

    if (!snapshot->points.empty()) {
        const auto& last = snapshot->points.back();
        ImGui::SameLine();
        ImGui::TextDisabled("%.6g  (%zu points)", last.value,
            snapshot->points.size());
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild("##plot-area", available, ImGuiChildFlags_Borders)) {
        if (snapshot->points.empty()) {
            ImGui::TextDisabled("No observations for this series yet.");
            ImGui::EndChild();
            ImGui::PopID();
            return;
        }

        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(snapshot->points.size());
        ys.reserve(snapshot->points.size());
        for (const auto& point : snapshot->points) {
            xs.push_back(static_cast<double>(point.at_ns) / 1.0e9);
            ys.push_back(point.value);
        }

        constexpr ImPlotFlags plot_flags = ImPlotFlags_None;
        constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_AutoFit;
        constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_AutoFit;

        if (ImPlot::BeginPlot("##series", ImVec2(-1.0f, -1.0f), plot_flags)) {
            ImPlot::SetupAxes("Time", "Value", x_flags, y_flags);
            /*
             * A series that spans years needs the year in its ticks; an
             * intraday one needs the minute. The span decides, because the
             * formatter sees one tick and not the axis.
             */
            const bool wide = xs.size() > 1
                && (xs.back() - xs.front()) > 90.0 * 24.0 * 3600.0;
            const char* pattern = wide ? "%d %b %Y" : "%d %b %H:%M";
            ImPlot::SetupAxisFormat(ImAxis_X1, format_time,
                const_cast<char*>(pattern));
            ImPlot::PlotLine(chart->label.c_str(), xs.data(), ys.data(),
                static_cast<int>(xs.size()));
            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}

}
