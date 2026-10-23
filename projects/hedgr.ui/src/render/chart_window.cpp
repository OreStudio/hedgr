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
#include "hedgr.ui/render/chart_plot.hpp"
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/plot_fill.hpp"
#include "hedgr.ui/render/series_text.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/plot_state.hpp"
#include "hedgr.ui/widget/segmented.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "ores.logging/make_logger.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.chart_window");
    return instance;
}

/** @brief The share of the x range kept clear after the last point. */
constexpr double chart_end_padding_share = 0.010;
/** @brief The share kept before the first point. */
constexpr double chart_start_padding_share = 0.20;
/** @brief The padding for a chart holding one point, in seconds. */
constexpr double chart_single_point_padding_s = 30.0;

/** @brief Draws a dashed line over the plot, as the web chart draws its EMA. */
void plot_dashed(const double* xs, const double* ys, std::size_t count,
    ImVec4 colour) {
    if (count < 2)
        return;
    auto* draw = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    constexpr float dash = 6.0f;
    constexpr float gap = 4.0f;
    bool drawing = true;
    float remaining = dash;
    for (std::size_t i = 0; i + 1 < count; ++i) {
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


/** @brief An axis tick, worded as the last price tag is. */
int format_price_tick(double value, char* buffer, int size, void*) {
    const auto text = format_price(value);
    const auto written = std::min(text.size, static_cast<std::size_t>(size - 1));
    std::copy_n(text.buffer.data(), written, buffer);
    buffer[written] = '\0';
    return static_cast<int>(written);
}

/**
 * @brief Draws a horizontal line across [@p left, @p right] as dashes, snapped
 * to the pixel row so a one pixel line is crisp.
 */
void dashed_hline(ImDrawList* draw, float left, float right, float y,
    ImU32 colour, float dash, float gap) {
    const float row = std::floor(y) + 0.5f;
    for (float x = left; x < right; x += dash + gap)
        draw->AddLine(ImVec2(x, row), ImVec2(std::min(x + dash, right), row),
            colour, 1.0f);
}

/** @brief A length of time in the fewest words the period combo uses. */
[[nodiscard]] std::string duration_text(std::int64_t ns) {
    const auto minutes = ns / domain::market_medium_ns;
    if (minutes < 60)
        return std::format("{}m", minutes);
    if (minutes < 48 * 60)
        return std::format("{}h", minutes / 60);
    return std::format("{}d", minutes / (24 * 60));
}

/**
 * @brief Asks the runtime to save the chart's choices as its own view.
 *
 * The window state is the runtime's to write, so the chart sends the view and
 * draws from its scratch until the saved view comes back on the next frame.
 */
void save_chart_view(window_handle handle, const render_input& input,
    window_ui_state& state) {
    hedgr::model::command::command request;
    request.kind = hedgr::model::command::command_kind::configure_chart;
    request.window_id = std::string{input.windows.id(handle)};
    request.chart_view = chart_view_of(state.market_view);
    input.commands.push(std::move(request));
    state.chart_view_sent_for = handle;
}

/**
 * @brief Saves the chart's view after one of its choices changed.
 *
 * Only the choice that changed updates the remembered default. Copying the
 * whole window state would take another window's untouched choices with it
 * and make the last click win every field.
 */
void chart_view_changed(window_handle handle, const render_input& input,
    window_ui_state& state, const market_view_state& before) {
    if (state.market_view.chart_span != before.chart_span)
        input.market_view.chart_span = state.market_view.chart_span;
    if (state.market_view.chart_style != before.chart_style)
        input.market_view.chart_style = state.market_view.chart_style;
    if (state.market_view.chart_ema != before.chart_ema)
        input.market_view.chart_ema = state.market_view.chart_ema;
    save_chart_view(handle, input, state);
}

/** @brief What the header states about the series, derived once a frame. */
struct chart_summary final {
    std::string_view label;
    double first = 0.0;
    double last = 0.0;
    domain::data_health health = domain::data_health::unavailable;
    std::size_t plotted = 0;
    std::int64_t span_ns = 0;
    hedgr::model::state::series_history history;
    bool no_points = true;
    /** @brief The time the plotted points cover, when short of the span. */
    std::optional<std::int64_t> short_shown_ns;

    [[nodiscard]] double change() const { return last - first; }
    [[nodiscard]] double percent() const {
        return first != 0.0 ? change() / first * 100.0 : 0.0;
    }
};

[[nodiscard]] const char* short_history_reason(
    const hedgr::model::state::series_history& history) {
    return history.truncated
        ? "The read stopped at its bound, so older observations were not read"
        : "The server holds no older observations for this series";
}

/**
 * @brief The full header: the series, what it is worth, and how many points
 * are in view, in one row with a large price.
 */
void draw_full_header(const chart_summary& s) {
    ImGui::AlignTextToFramePadding();
    widget::value_pill(s.label);
    if (!s.no_points) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::headline_value(format_price(s.last).view());
        ImGui::SameLine();
        widget::value_pill(format_number("{:+.2f}%", s.percent()).view(),
            change_tone(s.health, s.change()));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Points",
            "Observations plotted in the chosen period.");
        ImGui::SameLine();
        ImGui::Text("%zu", s.plotted);
        if (s.span_ns == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("\u00b7 All points");
            ImGui::SetItemTooltip("%s",
                "The period is All, so every point in the series is plotted");
        }
    }
    if (s.history.reading) {
        ImGui::SameLine();
        ImGui::TextDisabled("Reading older history...");
    } else if (s.short_shown_ns) {
        ImGui::SameLine();
        ImGui::TextDisabled("\u00b7 shows %s of %s",
            duration_text(*s.short_shown_ns).c_str(),
            duration_text(s.span_ns).c_str());
        ImGui::SetItemTooltip("%s", short_history_reason(s.history));
    }
}

/** @brief What a header icon asks for. */
enum class header_ask : std::uint8_t { none, dock, mini_plot };

/** @brief An icon at the right end of a chart's header, and what it asks. */
struct header_icon final {
    const char* id;
    const char* glyph;
    const char* tooltip;
    header_ask request;
};

constexpr header_icon mini_plot_icon{"mini", icon::dashboard,
    "Open a mini plot for this series", header_ask::mini_plot};

constexpr std::array docked_icons{mini_plot_icon};

constexpr std::array floating_icons{mini_plot_icon,
    header_icon{"dock", icon::panel, "Dock into the desk", header_ask::dock},
};

/** @brief What the header's controls asked, and the room they keep. */
struct header_icons_drawn final {
    header_ask asked = header_ask::none;
    bool view_changed = false;
    /** @brief How far short of the row's right end the flags end. */
    float flags_inset = 0.0f;
};

/**
 * @brief The icon that asks for the window to dock, drawn @p side square.
 */
[[nodiscard]] bool tile_icon(const header_icon& icon, ImVec2 origin,
    float side) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushID(icon.id);
    const bool pressed = ImGui::InvisibleButton(icon.glyph, ImVec2(side, side));
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", icon.tooltip);

    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + side, origin.y + side);
    if (hovered)
        draw->AddRectFilled(origin, max,
            ImGui::GetColorU32(ImGuiCol_ButtonHovered),
            ImGui::GetStyle().FrameRounding);
    const ImVec2 glyph = ImGui::CalcTextSize(icon.glyph);
    draw->AddText(ImVec2(origin.x + std::floor((side - glyph.x) * 0.5f),
                       origin.y + std::floor((side - glyph.y) * 0.5f)),
        ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled),
        icon.glyph);
    return pressed;
}

/**
 * @brief Draws the header's controls at the right end of the row, @p height
 * tall, and clips the rest of the row short of them.
 *
 * From the right: the @p icons, the room for the flags, then the @p strips in
 * order, so a narrow tile loses the end of its header text and never a control.
 * The caller draws the row and then pops the clip.
 */
[[nodiscard]] header_icons_drawn begin_header_row(
    std::span<const header_icon> icons,
    std::span<const widget::segmented_control> strips, float flags_width,
    float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float gap = style.ItemInnerSpacing.x;
    const float group_gap = style.ItemSpacing.x;
    const float side = widget::segmented_height();
    const float icons_width = icons.empty()
        ? 0.0f
        : static_cast<float>(icons.size()) * (side + gap) - gap;
    const float right = start.x + ImGui::GetContentRegionAvail().x;
    const float icons_left = std::max(start.x, right - icons_width);
    const float top = start.y + std::max(0.0f, std::floor((height - side) * 0.5f));

    header_icons_drawn drawn;
    drawn.flags_inset = icons.empty() ? 0.0f : icons_width + group_gap;
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const ImVec2 origin(icons_left + static_cast<float>(i) * (side + gap),
            top);
        if (tile_icon(icons[i], origin, side))
            drawn.asked = icons[i].request;
    }

    float left = icons_left - (icons.empty() ? 0.0f : group_gap);
    if (flags_width > 0.0f)
        left -= flags_width + group_gap;
    for (std::size_t i = strips.size(); i-- > 0;) {
        left = std::max(start.x, left - widget::segmented_width(strips[i]));
        if (widget::draw_segmented(strips[i], ImVec2(left, top)))
            drawn.view_changed = true;
        left -= group_gap;
    }

    ImGui::SetCursorScreenPos(start);
    ImGui::PushClipRect(start, ImVec2(left + group_gap - gap, FLT_MAX), true);
    return drawn;
}

/**
 * @brief The body and foot of a chart whose session is not open.
 *
 * The plot area says so with the shared glyph and caption, where the other data
 * windows use a sentence, because a chart is the window an operator watches
 * for liveness and a glyph reads at a glance.
 */
void draw_connection_state(const render_input& input,
    widget::status_bar& status, widget::foot_state& foot,
    widget::plot_state state, std::string_view reason) {
    set_health_foot(foot, domain::data_health::unavailable, 0, 0, false);
    const float foot_height = widget::status_foot_height(status, foot)
        + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##plot-area",
            ImVec2(available.x, std::max(available.y - foot_height, 80.0f)),
            ImGuiChildFlags_None)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 room = ImGui::GetContentRegionAvail();
        widget::draw_plot_state(state, at,
            ImVec2(at.x + room.x, at.y + room.y), theme::plot_state_glyph_size,
            true, state == widget::plot_state::failed ? reason
                                                      : std::string_view{});
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    widget::draw_status_foot(status, input.now_ns, foot);
}
}

/** @brief The colour a value takes for the plot's data health. */
[[nodiscard]] ImVec4 health_tint(domain::data_health health, ImVec4 live) {
    if (health == domain::data_health::stale)
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (health == domain::data_health::delayed)
        return ImGui::ColorConvertU32ToFloat4(health_colour(health));
    return live;
}

void render_chart(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    using hedgr::model::domain::chart_state;

    ImGui::PushID("chart");
    /*
     * The foot's identity is a few strings, so it is built only where a foot
     * is drawn.
     */
    const auto make_foot = [&] {
        widget::foot_state foot;
        foot.identity = identity_text(identity_of(input, handle));
        return foot;
    };

    const auto& shape = input.windows.state(handle);
    const auto* chart = std::get_if<chart_state>(&shape);
    if (chart == nullptr) {
        ImGui::TextWrapped("%s", "This window is not a chart.");
        widget::draw_status_foot(status, input.now_ns, make_foot());
        ImGui::PopID();
        return;
    }

    /*
     * A chart with no view of its own yet saves the defaults it opened on, so
     * another chart changing those defaults later does not move this one.
     */
    if (chart->view)
        apply_chart_view(*chart->view, state.market_view);
    else if (state.chart_view_sent_for != handle)
        save_chart_view(handle, input, state);

    const auto session = signed_in_session(input, handle);
    if (!session) {
        auto foot = make_foot();
        const auto bound = input.windows.session_of(handle);
        const auto state_now = bound
            ? widget::plot_state_for(input.sessions.status(*bound), false)
            : widget::plot_state::none;
        if (state_now == widget::plot_state::none) {
            draw_signed_out(input, status, foot);
        } else {
            draw_connection_state(input, status, foot, state_now,
                input.sessions.last_error(*bound));
        }
        ImGui::PopID();
        return;
    }
    const auto& series = input.market;
    const auto held = input.windows.series(handle);
    const auto row = !held.empty() && series.contains(held.front())
        ? std::optional{held.front()}
        : std::nullopt;
    const bool reading = !row
        || (series.observed_at_ns(*row) == 0 && series.failure(*row).empty());

    const auto span_ns = chart_span_ns(state.market_view.chart_span);
    const auto history = row ? series.history(*row)
                             : hedgr::model::state::series_history{};
    const hedgr::model::state::series_columns points = row
        ? series.columns(*row, domain::market_level_for_span(span_ns))
        : hedgr::model::state::series_columns{};
    const std::int64_t as_of_ns = row ? series.newest_at_ns(*row) : 0;
    const auto health = domain::assess_data_health(as_of_ns,
        input.wall_now_ns, domain::fx_health_thresholds);
    auto& plot = state.plot;
    const bool rebuilt = update_chart_plot(plot,
        chart_plot_key{.version = row ? series.version(*row) : 0,
            .span_ns = span_ns,
            .candles = state.market_view.chart_style == 0,
            .ema = state.market_view.chart_ema != 0},
        points);
    if (rebuilt) {
        const auto level = domain::market_level_for_span(span_ns);
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Chart '" << chart->series_id << "' replotted: span_ns="
            << span_ns << " level=" << static_cast<int>(level)
            << " row=" << (row ? "held" : "absent")
            << " ring_points=" << points.size
            << " oldest_at_ns="
            << (points.size ? points.at_ns[0] : 0)
            << " newest_at_ns="
            << (points.size ? points.at_ns[points.size - 1] : 0)
            << " plotted=" << (points.size - chart_plot_start(plot, points))
            << " candle_width_ns=" << plot.candle_width_ns
            << " candles=" << plot.candles.size()
            << " observed_at_ns=" << (row ? series.observed_at_ns(*row) : 0)
            << " history_reading=" << history.reading
            << " history_floor_ns=" << history.floor_ns
            << " history_truncated=" << history.truncated
            << " failure='"
            << (row ? std::string{series.failure(*row)} : std::string{})
            << "'";
    }
    const std::size_t start = chart_plot_start(plot, points);
    const std::size_t plotted = points.size - start;
    const double* const xs = points.at_s + start;
    const double* const ys = points.value + start;
    const bool no_points = points.size == 0;
    const double first = no_points ? 0.0 : points.value[start];
    const double last = no_points ? 0.0 : points.value[points.size - 1];
    const ImVec4 line_colour = theme::chart_line_colour();

    /*
     * The runtime decides whether the series already holds the span. The chart
     * only says which span it plots, once per change, and once more when a read
     * that was in flight ends cleanly, because a change made during it was not
     * started.
     */
    if (row && !reading) {
        if (state.history_was_reading && !history.reading
            && series.failure(*row).empty())
            state.history_asked_ns = -1;
        state.history_was_reading = history.reading;
        if (state.history_asked_ns != span_ns) {
            hedgr::model::command::command ask;
            ask.kind = hedgr::model::command::command_kind::load_history;
            ask.instance_id = std::string{input.sessions.instance_id(*session)};
            ask.account_id = std::string{input.sessions.account_id(*session)};
            ask.series_id = chart->series_id;
            ask.history_span_ns = span_ns;
            input.commands.push(std::move(ask));
            state.history_asked_ns = span_ns;
        }
    }
    chart_summary summary{.label = chart->label,
        .first = first,
        .last = last,
        .health = health,
        .plotted = plotted,
        .span_ns = span_ns,
        .history = history,
        .no_points = no_points};
    if (!history.reading && !no_points && span_ns > 0
        && history.floor_ns != 0) {
        const auto shown_ns = plotted > 1
            ? points.at_ns[points.size - 1] - points.at_ns[start] : 0;
        if (shown_ns + domain::market_level_ns(
                domain::market_level_for_span(span_ns)) < span_ns)
            summary.short_shown_ns = shown_ns;
    }

    /*
     * One header row above the plot, the same docked or floating. Docking is
     * ImGui's: the node's tab bar names the chart and a drag on it undocks.
     * The Dock icon is for a chart that is not docked yet.
     */
    const bool docked = ImGui::IsWindowDocked();
    const market_view_state view_before = state.market_view;
    const std::array strips{
        widget::segmented_control{.id = "period",
            .tooltip = "The span the chart plots",
            .options = chart_span_labels.data(),
            .option_count = static_cast<int>(chart_span_labels.size()),
            .selected = &state.market_view.chart_span},
        widget::segmented_control{.id = "style",
            .tooltip = "Draw candles or a line",
            .options = chart_style_labels.data(),
            .option_count = static_cast<int>(chart_style_labels.size()),
            .selected = &state.market_view.chart_style},
        widget::segmented_control{.id = "ema",
            .tooltip = "Overlay the exponential moving average of the window",
            .options = chart_ema_labels.data(),
            .option_count = static_cast<int>(chart_ema_labels.size()),
            .selected = &state.market_view.chart_ema}};
    const float flags_width = series_flags_width(input, *session, chart->label);
    const auto icons = begin_header_row(
        docked ? std::span<const header_icon>{docked_icons}
               : std::span<const header_icon>{floating_icons},
        strips, flags_width, ImGui::GetFrameHeight());
    state.tile = icons.asked == header_ask::dock ? tile_request::dock
                                                 : tile_request::none;
    if (icons.asked == header_ask::mini_plot) {
        request_series_window(input, *session, domain::window_kind::mini_plot,
            chart->series_id, chart->label,
            chart_span_ns(state.market_view.chart_span));
    }
    draw_full_header(summary);
    ImGui::SameLine();
    ImGui::PopClipRect();
    /*
     * The flags sit at a fixed place short of the icons, not after the text,
     * so a long header never pushes them over a control.
     */
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x
        - icons.flags_inset - flags_width);
    draw_series_flags(input, *session, chart->label, icons.flags_inset);
    if (icons.view_changed)
        chart_view_changed(handle, input, state, view_before);

    if (row && !series.failure(*row).empty()) {
        const auto failure = series.failure(*row);
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%.*s", static_cast<int>(failure.size()), failure.data());
    }

    std::optional<widget::foot_state> foot;
    float foot_height = 0.0f;
    {
        foot = make_foot();
        set_health_foot(*foot, health, as_of_ns, input.wall_now_ns, reading);
        if (row)
            set_history_unavailable_foot(*foot, series.failure(*row));
        foot_height = widget::status_foot_height(status, *foot)
            + ImGui::GetStyle().ItemSpacing.y;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(available.y - foot_height, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##plot-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (reading) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 room = ImGui::GetContentRegionAvail();
            widget::draw_plot_state(widget::plot_state::reading, at,
                ImVec2(at.x + room.x, at.y + room.y),
                theme::plot_state_glyph_size, true);
        } else if (no_points) {
            ImGui::TextDisabled("No observations for this series yet.");
        } else {
            const auto& candle_points = plot.candles;
            const bool use_candles = !candle_points.empty();
            const double low = plot.low;
            const double high = plot.high;
            const double span = high - low > 0.0
                ? high - low
                : std::max(std::abs(high) * 0.005, 1e-6);
            const double floor = low - span * 0.08;

            constexpr ImPlotFlags plot_flags = ImPlotFlags_Crosshairs
                | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend
                | ImPlotFlags_NoMenus | ImPlotFlags_NoFrame;
            /*
             * No grid of ImPlot's own: its rules are solid and run both ways,
             * and the plot draws its dashed horizontal ones itself.
             */
            constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_NoGridLines
                | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoHighlight;
            constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_Opposite
                | x_flags;

            const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(2.0f, 2.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
            ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding,
                ImVec2(theme::plot_label_padding_x,
                    theme::plot_label_padding_y));
            ImPlot::PushStyleColor(ImPlotCol_FrameBg, clear);
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, clear);
            ImPlot::PushStyleColor(ImPlotCol_PlotBorder, clear);
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                ImGui::ColorConvertU32ToFloat4(theme::plot_grid_colour));
            ImPlot::PushStyleColor(ImPlotCol_AxisText, theme::dim_text());
            ImPlot::PushStyleColor(ImPlotCol_Crosshairs,
                ImGui::ColorConvertU32ToFloat4(theme::plot_crosshair_colour));
            ImGui::PushFont(icon::numeric_font());
            if (ImPlot::BeginPlot("##series", ImVec2(-1.0f, -1.0f),
                    plot_flags)) {
                ImPlot::SetupAxes("", "", x_flags, y_flags);
                /*
                 * A series that spans years needs the year in its ticks; an
                 * intraday one needs the minute. The span decides, because the
                 * formatter sees one tick and not the axis.
                 */
                const double seconds = plotted > 1
                    ? xs[plotted - 1] - xs[0] : 0.0;
                const char* pattern = seconds <= 15.0 * 60.0 ? "%H:%M:%S"
                    : seconds <= 36.0 * 3600.0 ? "%H:%M"
                    : seconds <= 90.0 * 24.0 * 3600.0 ? "%a %d"
                    : "%b %Y";
                ImPlot::SetupAxisFormat(ImAxis_X1, format_time,
                    const_cast<char*>(pattern));
                ImPlot::SetupAxisFormat(ImAxis_Y1, format_price_tick, nullptr);
                const double width_s =
                    static_cast<double>(plot.candle_width_ns) / 1.0e9;
                double x_min = 0.0;
                double x_max = 0.0;
                if (use_candles) {
                    double candle_low = candle_points.front().low;
                    double candle_high = candle_points.front().high;
                    for (const auto& candle : candle_points) {
                        candle_low = std::min(candle_low, candle.low);
                        candle_high = std::max(candle_high, candle.high);
                    }
                    const double pad = (candle_high - candle_low) * 0.08;
                    x_min = static_cast<double>(candle_points.front().at_ns)
                            / 1.0e9 - width_s;
                    x_max = static_cast<double>(candle_points.back().at_ns)
                            / 1.0e9 + width_s * 2.0;
                    ImPlot::SetupAxisLimits(ImAxis_Y1, candle_low - pad,
                        candle_high + pad, ImPlotCond_Always);
                } else {
                    const double x_span = plotted > 1
                        ? xs[plotted - 1] - xs[0] : 0.0;
                    const double x_pad = x_span > 0.0
                        ? x_span * chart_end_padding_share
                        : chart_single_point_padding_s;
                    x_min = xs[0] - x_pad * chart_start_padding_share;
                    x_max = xs[plotted - 1] + x_pad;
                    ImPlot::SetupAxisLimits(ImAxis_Y1, floor,
                        high + span * 0.08, ImPlotCond_Always);
                }
                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max,
                    ImPlotCond_Always);

                /*
                 * The bottom axis names a few places in the span, away from
                 * the edges so a label is never cut off by the plot's.
                 */
                std::array<double, theme::plot_x_label_count> places{};
                for (std::size_t i = 0; i < places.size(); ++i)
                    places[i] = x_min + (x_max - x_min)
                        * (static_cast<double>(i) + 0.5)
                        / static_cast<double>(places.size());
                ImPlot::SetupAxisTicks(ImAxis_X1, places.data(),
                    static_cast<int>(places.size()));
                ImPlot::SetupFinish();

                const ImVec4 tag_colour = health_tint(health, theme::chart_tag_colour());

                /*
                 * The grid is the horizontal rules at the Y ticks, dashed and
                 * faint, under everything the plot draws.
                 */
                {
                    auto* rules = ImPlot::GetPlotDrawList();
                    ImPlot::PushPlotClipRect();
                    const ImVec2 origin = ImPlot::GetPlotPos();
                    const float right = origin.x + ImPlot::GetPlotSize().x;
                    for (const auto& tick :
                        ImPlot::GetCurrentPlot()->Axes[ImAxis_Y1].Ticker.Ticks)
                        dashed_hline(rules, origin.x, right,
                            ImPlot::PlotToPixels(0.0, tick.PlotPos).y,
                            theme::plot_grid_colour, theme::plot_grid_dash,
                            theme::plot_grid_gap);
                    ImPlot::PopPlotClipRect();
                }

                if (use_candles) {
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
                    fill_gradient_under_curve(xs, ys,
                        static_cast<int>(plotted),
                        ImGui::ColorConvertFloat4ToU32(line_colour), 0.32f);
                }

                /*
                 * The last price is a line across the plot and a tag on the
                 * axis, which is how a trading screen states it. The tag reads
                 * the same number as the header.
                 */
                {
                    auto* rule = ImPlot::GetPlotDrawList();
                    ImPlot::PushPlotClipRect();
                    const ImVec2 origin = ImPlot::GetPlotPos();
                    ImVec4 dotted = tag_colour;
                    dotted.w = theme::plot_price_line_alpha;
                    dashed_hline(rule, origin.x, origin.x + ImPlot::GetPlotSize().x,
                        ImPlot::PlotToPixels(0.0, last).y,
                        ImGui::ColorConvertFloat4ToU32(dotted),
                        theme::plot_price_dot, theme::plot_price_gap);
                    ImPlot::PopPlotClipRect();
                }

                if (!use_candles) {
                    ImPlotSpec line;
                    line.LineColor = line_colour;
                    line.LineWeight = theme::plot_line_weight;
                    ImPlot::PlotLine("##line", xs, ys,
                        static_cast<int>(plotted), line);
                }

                if (!plot.ema.empty() && plot.ema.size() == plotted) {
                    plot_dashed(xs, plot.ema.data(), plotted,
                        ImGui::ColorConvertU32ToFloat4(
                            theme::moving_average_colour));
                }

                const auto point = ImPlot::PlotToPixels(xs[plotted - 1],
                    ys[plotted - 1]);
                auto* plot_draw = ImPlot::GetPlotDrawList();
                ImPlot::PushPlotClipRect();
                if (health == domain::data_health::live) {
                    const auto phase = static_cast<float>(
                        static_cast<double>(input.now_ns % 1'000'000'000) / 1.0e9);
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
            ImPlot::PopStyleColor(6);
            ImPlot::PopStyleVar(3);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const auto before = state.market_view;
    if (widget::draw_status_foot(status, input.now_ns, *foot))
        chart_view_changed(handle, input, state, before);
    ImGui::PopID();
}

}
