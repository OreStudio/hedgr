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
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/data_health.hpp"
#include "hedgr.ui/render/ema.hpp"
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/plot_fill.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <ctime>
#include <format>
#include <string>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

/** @brief The share of the x range kept clear after the last point. */
constexpr double chart_end_padding_share = 0.010;
/** @brief The share kept before the first point. */
constexpr double chart_start_padding_share = 0.20;
/** @brief The padding for a chart holding one point, in seconds. */
constexpr double chart_single_point_padding_s = 30.0;

/** @brief Draws a dashed line over the plot, as the web chart draws its EMA. */
void plot_dashed(const std::vector<double>& xs,
    const std::vector<double>& ys, ImVec4 colour) {
    if (xs.size() < 2 || xs.size() != ys.size())
        return;
    auto* draw = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    constexpr float dash = 6.0f;
    constexpr float gap = 4.0f;
    bool drawing = true;
    float remaining = dash;
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        const auto a = ImPlot::PlotToPixels(xs[i], ys[i]);
        const auto b = ImPlot::PlotToPixels(xs[i + 1], ys[i + 1]);
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f)
            continue;
        const float ux = dx / length;
        const float uy = dy / length;
        float at = 0.0f;
        while (at < length) {
            const float step = std::min(remaining, length - at);
            if (drawing) {
                draw->AddLine(ImVec2(a.x + ux * at, a.y + uy * at),
                    ImVec2(a.x + ux * (at + step), a.y + uy * (at + step)),
                    ImGui::ColorConvertFloat4ToU32(colour), 1.5f);
            }
            at += step;
            remaining -= step;
            if (remaining <= 0.0f) {
                drawing = !drawing;
                remaining = drawing ? dash : gap;
            }
        }
    }
    ImPlot::PopPlotClipRect();
}

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

/**
 * @brief How old a series may be and still be read as a live feed.
 *
 * A series whose newest point is older than this is history, and history is
 * plotted whole whatever span is chosen: the imported vendor series end years
 * ago, and cropping one to five minutes would leave two points and a lie.
 */
constexpr std::int64_t live_threshold_ns =
    std::int64_t{10} * 60 * 1'000'000'000;

/** @brief The history level a span reads from. */
[[nodiscard]] domain::market_level level_for(std::int64_t span_ns) {
    if (span_ns == 0)
        return domain::market_level::coarse;
    if (span_ns <= 2 * domain::market_coarse_ns)
        return domain::market_level::fine;
    if (span_ns <= 2 * 24 * domain::market_coarse_ns)
        return domain::market_level::medium;
    return domain::market_level::coarse;
}

/** @brief The first index of the points that fall in @p span_ns. */
[[nodiscard]] std::size_t live_window_start(
    const std::deque<domain::market_point>& points, std::int64_t span_ns) {
    if (points.size() < 2 || span_ns <= 0)
        return 0;
    const auto cutoff = points.back().at_ns - span_ns;
    std::size_t first = 0;
    while (first + 1 < points.size() && points[first].at_ns < cutoff)
        ++first;
    /* Two points are the least a line needs; a sparse series keeps it all. */
    return points.size() - first < 2 ? 0 : first;
}

/** @brief One candle, built from the points of its interval. */
struct candle final {
    std::int64_t at_ns = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

/** @brief How wide a candle is, and which ring it is built from. */
struct candle_plan final {
    std::int64_t width_ns = 0;
    domain::market_level level = domain::market_level::fine;
};

/**
 * @brief The candle width a span reads best at.
 *
 * Around sixty candles fill the width without turning into a smear. The
 * finer rings carry one second buckets, the medium ring one minute and the
 * coarse ring one hour, so a wider candle combines the ring beneath it.
 */
[[nodiscard]] candle_plan candle_plan_for(std::int64_t span_ns) {
    if (span_ns <= 60 * domain::market_fine_ns)
        return {.width_ns = domain::market_fine_ns,
            .level = domain::market_level::fine};
    if (span_ns <= 300 * domain::market_fine_ns)
        return {.width_ns = 5 * domain::market_fine_ns,
            .level = domain::market_level::fine};
    if (span_ns <= 900 * domain::market_fine_ns)
        return {.width_ns = 15 * domain::market_fine_ns,
            .level = domain::market_level::fine};
    if (span_ns <= 3600 * domain::market_fine_ns)
        return {.width_ns = domain::market_medium_ns,
            .level = domain::market_level::medium};
    if (span_ns <= 24 * 3600 * domain::market_fine_ns)
        return {.width_ns = 15 * domain::market_medium_ns,
            .level = domain::market_level::medium};
    return {.width_ns = 24 * domain::market_coarse_ns,
        .level = domain::market_level::coarse};
}

/** @brief Combines ring points into candles of @p width_ns. */
[[nodiscard]] std::vector<candle> build_candles(
    const std::deque<domain::market_point>& points, std::size_t start,
    std::int64_t width_ns) {
    std::vector<candle> candles;
    if (width_ns <= 0)
        return candles;
    for (std::size_t i = start; i < points.size(); ++i) {
        const auto& point = points[i];
        const double open = point.open != 0.0 || point.high != 0.0
                || point.low != 0.0
            ? point.open
            : point.value;
        const double high = point.high != 0.0 ? point.high : point.value;
        const double low = point.low != 0.0 ? point.low : point.value;
        const auto bucket = point.at_ns / width_ns;
        if (candles.empty() || candles.back().at_ns / width_ns != bucket) {
            candles.push_back(candle{.at_ns = bucket * width_ns,
                .open = open, .high = high, .low = low, .close = point.value});
            continue;
        }
        auto& last = candles.back();
        last.high = std::max(last.high, high);
        last.low = std::min(last.low, low);
        last.close = point.value;
    }
    return candles;
}

}

/** @brief The colour a value takes for the plot's data health. */
[[nodiscard]] ImVec4 health_tint(
    const hedgr::ui::render::data_health_state& health, ImVec4 live) {
    if (health.state == hedgr::ui::render::data_health::stale)
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (health.state == hedgr::ui::render::data_health::delayed)
        return ImGui::ColorConvertU32ToFloat4(
            theme::readiness_colour(domain::readiness::bootstrapping));
    return live;
}

void render_chart(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
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

    const auto span_ns = chart_span_ns(state.market_view.chart_span);
    static const std::deque<domain::market_point> no_points;
    const auto& points = snapshot != nullptr
        ? snapshot->level(level_for(span_ns))
        : no_points;
    const std::int64_t newest_at_ns =
        points.empty() ? 0 : points.back().at_ns;
    const auto health = assess_data_health(newest_at_ns,
        input.wall_now_ns, fx_health_thresholds, fx_history_ns);
    const bool recent = newest_at_ns != 0 && input.wall_now_ns >= newest_at_ns
        && input.wall_now_ns - newest_at_ns <= live_threshold_ns;
    const std::size_t start = live_window_start(points,
        recent ? span_ns : 0);
    const double first = points.empty() ? 0.0 : points[start].value;
    const double last = points.empty() ? 0.0 : points.back().value;
    const double change = last - first;
    const ImU32 colour = change < 0.0 ? theme::failure_colour : up_colour;
    const ImVec4 line_colour = ImGui::ColorConvertU32ToFloat4(colour);
    const bool candles = state.market_view.chart_style == 0;
    const bool ema_enabled = state.market_view.chart_ema != 0;

    foot.word = std::string{data_health_word(health.state)};
    foot.colour = health_colour(health.state);
    foot.as_of = as_of_text(newest_at_ns);
    foot.controls.push_back(widget::foot_combo{
        .id = "period",
        .tooltip = "The span the chart plots",
        .options = chart_span_labels.data(),
        .option_count = static_cast<int>(chart_span_labels.size()),
        .selected = &state.market_view.chart_span});
    foot.controls.push_back(widget::foot_combo{
        .id = "style",
        .tooltip = "Draw candles or a line",
        .options = chart_style_labels.data(),
        .option_count = static_cast<int>(chart_style_labels.size()),
        .selected = &state.market_view.chart_style});
    foot.controls.push_back(widget::foot_combo{
        .id = "ema",
        .tooltip = "Overlay the exponential moving average of the window",
        .options = chart_ema_labels.data(),
        .option_count = static_cast<int>(chart_ema_labels.size()),
        .selected = &state.market_view.chart_ema});

    /*
     * One row of chrome above the plot: the series, what it is worth, and how
     * many points are in view. Everything that is a setting lives on the foot,
     * so the plot gets the rest of the window.
     */
    ImGui::TextUnformatted(chart->label.c_str());
    if (!points.empty()) {
        const double percent =
            first != 0.0 ? change / first * 100.0 : 0.0;
        const ImVec4 tint = health_tint(health,
            ImGui::ColorConvertU32ToFloat4(colour));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::PushFont(icon::numeric_font());
        ImGui::TextColored(tint, "%s", format_price(last).c_str());
        ImGui::SameLine();
        ImGui::TextColored(tint, "%+.2f%%", percent);
        ImGui::PopFont();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextDisabled("%zu points", points.size() - start);
        if (!recent) {
            ImGui::SameLine();
            ImGui::TextDisabled("\u00b7 All points");
            ImGui::SetItemTooltip("%s",
                "The newest point is older than ten minutes, so the series "
                "is history and is plotted whole");
        }
    }
    if (session.has_value()) {
        ImGui::SameLine();
        draw_series_flags(input, *session, chart->label);
    }

    if (snapshot != nullptr && !snapshot->failure.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", snapshot->failure.c_str());
    }

    const float foot_height = widget::status_foot_height(status, foot);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(
        available.y - foot_height - ImGui::GetStyle().ItemSpacing.y, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##plot-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (snapshot == nullptr
            || (!snapshot->is_read() && snapshot->failure.empty())) {
            ImGui::TextDisabled("Reading the series...");
        } else if (points.empty()) {
            ImGui::TextDisabled("No observations for this series yet.");
        } else {
            /*
             * A historical series is plotted whole, so its candles are sized
             * from the history it actually has rather than from a span that
             * does not apply.
             */
            const auto candle_span = recent
                ? span_ns
                : points.back().at_ns - points[start].at_ns;
            const auto plan = candle_plan_for(candle_span);
            const auto candle_points = candles
                ? build_candles(points, start, plan.width_ns)
                : std::vector<candle>{};
            const bool use_candles = candles && !candle_points.empty();

            std::vector<double> xs;
            std::vector<double> ys;
            xs.reserve(points.size() - start);
            ys.reserve(points.size() - start);
            double low = last;
            double high = last;
            for (std::size_t i = start; i < points.size(); ++i) {
                const auto& point = points[i];
                xs.push_back(static_cast<double>(point.at_ns) / 1.0e9);
                ys.push_back(point.value);
                low = std::min(low, point.value);
                high = std::max(high, point.value);
            }
            const double span = high - low > 0.0
                ? high - low
                : std::max(std::abs(high) * 0.005, 1e-6);
            const double floor = low - span * 0.08;

            std::vector<double> ema;
            if (ema_enabled && ys.size() > 1)
                ema = exponential_moving_average(ys);

            constexpr ImPlotFlags plot_flags = ImPlotFlags_Crosshairs
                | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend
                | ImPlotFlags_NoMenus;
            constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_None;
            constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_Opposite;

            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(2.0f, 2.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                ImVec4(0.33f, 0.35f, 0.38f, 0.35f));
            ImGui::PushFont(icon::numeric_font());
            if (ImPlot::BeginPlot("##series", ImVec2(-1.0f, -1.0f),
                    plot_flags)) {
                ImPlot::SetupAxes("", "", x_flags, y_flags);
                /*
                 * A series that spans years needs the year in its ticks; an
                 * intraday one needs the minute. The span decides, because the
                 * formatter sees one tick and not the axis.
                 */
                const double seconds = xs.size() > 1
                    ? xs.back() - xs.front() : 0.0;
                const char* pattern = seconds <= 6.0 * 3600.0 ? "%H:%M:%S"
                    : seconds <= 90.0 * 24.0 * 3600.0 ? "%d %b %H:%M"
                    : "%d %b %Y";
                ImPlot::SetupAxisFormat(ImAxis_X1, format_time,
                    const_cast<char*>(pattern));
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");
                if (!use_candles) {
                    const double x_span = xs.size() > 1
                        ? xs.back() - xs.front() : 0.0;
                    const double x_pad = x_span > 0.0
                        ? x_span * chart_end_padding_share
                        : chart_single_point_padding_s;
                    ImPlot::SetupAxisLimits(ImAxis_X1,
                        xs.front() - x_pad * chart_start_padding_share,
                        xs.back() + x_pad,
                        ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, floor,
                        high + span * 0.08, ImPlotCond_Always);
                }

                if (use_candles) {
                    double candle_low = candle_points.front().low;
                    double candle_high = candle_points.front().high;
                    for (const auto& candle : candle_points) {
                        candle_low = std::min(candle_low, candle.low);
                        candle_high = std::max(candle_high, candle.high);
                    }
                    const double pad = (candle_high - candle_low) * 0.08;
                    const double width_s =
                        static_cast<double>(plan.width_ns) / 1.0e9;
                    ImPlot::SetupAxisLimits(ImAxis_X1,
                        static_cast<double>(candle_points.front().at_ns) / 1.0e9
                            - width_s,
                        static_cast<double>(candle_points.back().at_ns) / 1.0e9
                            + width_s * 2.0,
                        ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, candle_low - pad,
                        candle_high + pad, ImPlotCond_Always);

                    auto* plot_draw = ImPlot::GetPlotDrawList();
                    ImPlot::PushPlotClipRect();
                    for (const auto& candle : candle_points) {
                        const double x =
                            static_cast<double>(candle.at_ns) / 1.0e9;
                        const auto p_open = ImPlot::PlotToPixels(x, candle.open);
                        const auto p_close =
                            ImPlot::PlotToPixels(x, candle.close);
                        const auto p_high = ImPlot::PlotToPixels(x, candle.high);
                        const auto p_low = ImPlot::PlotToPixels(x, candle.low);
                        const ImU32 body_colour = candle.close >= candle.open
                            ? up_colour
                            : theme::failure_colour;
                        plot_draw->AddLine(ImVec2(p_open.x, p_high.y),
                            ImVec2(p_open.x, p_low.y), body_colour, 1.0f);
                        const float body_half = std::max(
                            (ImPlot::PlotToPixels(x + width_s, candle.open).x
                                - p_open.x) * 0.65f * 0.5f,
                            0.5f);
                        const float top = std::min(p_open.y, p_close.y);
                        const float bottom = std::max(p_open.y, p_close.y);
                        if (bottom - top < 1.0f) {
                            plot_draw->AddLine(ImVec2(p_open.x - body_half, top),
                                ImVec2(p_open.x + body_half, top), body_colour,
                                1.5f);
                        } else {
                            plot_draw->AddRectFilled(
                                ImVec2(p_open.x - body_half, top),
                                ImVec2(p_open.x + body_half, bottom),
                                body_colour);
                        }
                    }
                    ImPlot::PopPlotClipRect();
                } else {
                    /*
                     * The fill reaches the bottom of the range rather than
                     * zero: an FX rate is nowhere near zero, and a fill to
                     * zero would be the whole plot rather than the area under
                     * the line.
                     */
                    fill_gradient_under_curve(xs.data(), ys.data(),
                        static_cast<int>(xs.size()),
                        ImGui::ColorConvertFloat4ToU32(line_colour), 0.32f);
                }

                /*
                 * The last price is a line across the plot and a tag on the
                 * axis, which is how a trading screen states it. The tag reads
                 * the same number as the header.
                 */
                const ImVec4 tag_colour = health_tint(health, line_colour);
                ImPlotSpec last_line;
                last_line.LineColor = tag_colour;
                last_line.LineWeight = 1.0f;
                last_line.Flags = static_cast<ImPlotItemFlags>(
                    static_cast<int>(ImPlotItemFlags_NoLegend)
                    | static_cast<int>(ImPlotInfLinesFlags_Horizontal));
                ImPlot::PlotInfLines("##last", &last, 1, last_line);

                if (!use_candles) {
                    ImPlotSpec line;
                    line.LineColor = line_colour;
                    line.LineWeight = 1.5f;
                    ImPlot::PlotLine("##line", xs.data(), ys.data(),
                        static_cast<int>(xs.size()), line);
                }

                if (!ema.empty() && ema.size() == xs.size()) {
                    plot_dashed(xs, ema,
                        ImGui::ColorConvertU32ToFloat4(
                            theme::moving_average_colour));
                }

                const auto point = ImPlot::PlotToPixels(xs.back(),
                    ys.back());
                auto* plot_draw = ImPlot::GetPlotDrawList();
                ImPlot::PushPlotClipRect();
                if (health.state == hedgr::ui::render::data_health::live) {
                    const auto phase = static_cast<float>(
                        (input.now_ns % 1'000'000'000) / 1.0e9);
                    const float pulse = 0.5f
                        + 0.5f * std::sin(phase * 6.2831853f);
                    const float radius = 2.5f + pulse * 3.5f;
                    const ImVec4 glow(line_colour.x, line_colour.y,
                        line_colour.z, 0.18f + pulse * 0.45f);
                    plot_draw->AddCircleFilled(point, radius,
                        ImGui::ColorConvertFloat4ToU32(glow), 24);
                    plot_draw->AddCircleFilled(point, 2.0f,
                        ImGui::ColorConvertFloat4ToU32(line_colour), 16);
                } else {
                    const ImVec4 dot(tag_colour.x, tag_colour.y,
                        tag_colour.z, 0.95f);
                    plot_draw->AddCircleFilled(point, 3.0f,
                        ImGui::ColorConvertFloat4ToU32(dot), 16);
                }
                ImPlot::PopPlotClipRect();

                ImPlot::TagY(last, tag_colour, "%s",
                    format_price(last).c_str());
                ImPlot::EndPlot();
            }
            ImGui::PopFont();
            ImPlot::PopStyleColor();
            ImPlot::PopStyleVar(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const auto before = state.market_view;
    if (widget::draw_status_foot(status, input.now_ns, foot)) {
        /*
         * Only the choice that changed updates the remembered default. Copying
         * the whole window state would take another window's untouched choices
         * with it and make the last click win every field.
         */
        if (state.market_view.chart_span != before.chart_span)
            input.market_view.chart_span = state.market_view.chart_span;
        if (state.market_view.chart_style != before.chart_style)
            input.market_view.chart_style = state.market_view.chart_style;
        if (state.market_view.chart_ema != before.chart_ema)
            input.market_view.chart_ema = state.market_view.chart_ema;
    }
    ImGui::PopID();
}

}
