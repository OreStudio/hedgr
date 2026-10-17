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
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <format>
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

namespace {

constexpr ImU32 up_colour = IM_COL32(38, 166, 91, 255);

/**
 * @brief A price, with the decimals its magnitude needs.
 *
 * An FX pair quotes to five places and a rate to two, and the plot has no way
 * to ask which is which: the magnitude answers, and the alternative is a label
 * reading "1.23456" beside one reading "0.00000".
 */
[[nodiscard]] std::string format_price(double value) {
    const double magnitude = std::fabs(value);
    const int decimals = magnitude >= 1000.0 ? 2
        : magnitude >= 100.0 ? 3
        : magnitude >= 10.0 ? 4
        : 5;
    return std::format("{:.{}f}", value, decimals);
}

/** @brief The series as flags and its name, the header's left end. */
void draw_series_name(std::string_view label) {
    const auto pair = widget::currencies_of(label);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float x = origin.x;
    const float y = origin.y + (ImGui::GetTextLineHeight()
        - widget::flag_height) * 0.5f;
    auto* draw = ImGui::GetWindowDrawList();
    if (!pair.base.empty()) {
        widget::draw_flag(draw, ImVec2(x, y), pair.base);
        x += widget::flag_width + 2.0f;
    }
    if (!pair.quote.empty()) {
        widget::draw_flag(draw, ImVec2(x, y), pair.quote);
        x += widget::flag_width + 2.0f;
    }
    if (x == origin.x) {
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        return;
    }
    ImGui::Dummy(ImVec2(x - origin.x + 4.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
}

}

void render_chart(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    static_cast<void>(state);
    using hedgr::model::domain::chart_state;

    ImGui::PushID("chart");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* chart = std::get_if<chart_state>(&shape);
    if (chart == nullptr) {
        ImGui::TextWrapped("%s", "This window is not a chart.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const auto session = input.windows.session_of(handle);
    const auto* snapshot = session.has_value()
        ? input.market.points(input.sessions.instance_id(*session),
            input.sessions.account_id(*session), chart->series_id)
        : nullptr;

    /*
     * One row of chrome above the plot and no more. A trading screen is read at
     * a glance, so what the reader needs is the series, the last price, the
     * change and the freshness, in that order, on one line.
     */
    if (widget::icon_button("refresh", icon::refresh,
            "Read the series again")) {
        input.commands.push(command{
            .kind = command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    }
    ImGui::SameLine();
    draw_series_name(chart->label);

    if (snapshot != nullptr && !snapshot->points.empty()) {
        const auto& points = snapshot->points;
        const double first = points.front().value;
        const double last = points.back().value;
        const double change = last - first;
        const double percent = first != 0.0 ? change / first * 100.0 : 0.0;
        const ImU32 colour = change < 0.0 ? theme::failure_colour : up_colour;
        const ImVec4 tint = ImGui::ColorConvertU32ToFloat4(colour);

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextColored(tint, "%s", format_price(last).c_str());

        ImGui::SameLine();
        ImGui::TextColored(tint, "%+.2f%%", percent);

        ImGui::SameLine();
        ImGui::TextDisabled("%zu points", points.size());

        if (snapshot->observed_at_ns != 0
            && input.now_ns >= snapshot->observed_at_ns) {
            const auto age_s = static_cast<double>(
                input.now_ns - snapshot->observed_at_ns) / 1.0e9;
            const ImU32 poll_colour = age_s > 10.0
                ? theme::readiness_colour(domain::readiness::bootstrapping)
                : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(poll_colour),
                "polled %.0f s ago", age_s);
            ImGui::SetItemTooltip("%s",
                "The window reads the series on its cadence; this is when the "
                "last read finished");
        }
    }

    if (snapshot != nullptr && !snapshot->failure.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", snapshot->failure.c_str());
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##plot-area", available, ImGuiChildFlags_None)) {
        if (snapshot == nullptr
            || (!snapshot->is_read() && snapshot->failure.empty())) {
            ImGui::TextDisabled("Reading the series...");
        } else if (snapshot->points.empty()) {
            ImGui::TextDisabled("No observations for this series yet.");
        } else {
            const auto& points = snapshot->points;
            const double first = points.front().value;
            const double last = points.back().value;
            const double change = last - first;
            const ImU32 colour = change < 0.0 ? theme::failure_colour : up_colour;
            const ImVec4 line_colour = ImGui::ColorConvertU32ToFloat4(colour);

            std::vector<double> xs;
            std::vector<double> ys;
            xs.reserve(points.size());
            ys.reserve(points.size());
            double low = last;
            double high = last;
            for (const auto& point : points) {
                xs.push_back(static_cast<double>(point.at_ns) / 1.0e9);
                ys.push_back(point.value);
                low = std::min(low, point.value);
                high = std::max(high, point.value);
            }
            const double span = high - low > 0.0 ? high - low : 1.0;
            const double floor = low - span * 0.08;

            constexpr ImPlotFlags plot_flags = ImPlotFlags_Crosshairs
                | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend
                | ImPlotFlags_NoMenus;
            constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_AutoFit
                | ImPlotAxisFlags_NoGridLines;
            constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_AutoFit
                | ImPlotAxisFlags_Opposite;

            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(2.0f, 2.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
            if (ImPlot::BeginPlot("##series", ImVec2(-1.0f, -1.0f),
                    plot_flags)) {
                ImPlot::SetupAxes("", "", x_flags, y_flags);
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
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");

                /*
                 * The fill reaches the bottom of the range rather than zero:
                 * an FX rate is nowhere near zero, and a fill to zero would be
                 * the whole plot rather than the area under the line.
                 */
                ImPlotSpec fill;
                fill.FillColor = line_colour;
                fill.FillAlpha = 0.16f;
                fill.Flags = ImPlotItemFlags_NoLegend;
                ImPlot::PlotShaded("##fill", xs.data(), ys.data(),
                    static_cast<int>(xs.size()), floor, fill);

                /*
                 * The last price is a line across the plot and a tag on the
                 * axis, which is how a trading screen states it. The tag reads
                 * the same number as the header.
                 */
                ImPlotSpec last_line;
                last_line.LineColor = line_colour;
                last_line.LineWeight = 1.0f;
                last_line.Flags = static_cast<ImPlotItemFlags>(
                    static_cast<int>(ImPlotItemFlags_NoLegend)
                    | static_cast<int>(ImPlotInfLinesFlags_Horizontal));
                ImPlot::PlotInfLines("##last", &last, 1, last_line);

                ImPlotSpec line;
                line.LineColor = line_colour;
                line.LineWeight = 1.5f;
                ImPlot::PlotLine("##line", xs.data(), ys.data(),
                    static_cast<int>(xs.size()), line);

                ImPlot::TagY(last, line_colour, "%s",
                    format_price(last).c_str());
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleVar(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
