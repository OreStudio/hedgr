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
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "ores.logging/make_logger.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
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

/** @brief The gap under a compact chart's header row, tighter than a full one's. */
constexpr float compact_row_gap = 2.0f;

/**
 * @brief A price, with the decimals its magnitude needs.
 *
 * An FX pair quotes to five places and a rate to two, and the plot has no way
 * to ask which is which: the magnitude answers, and the alternative is a label
 * reading "1.23456" beside one reading "0.00000".
 */
/** @brief A number formatted into storage of its own, so a frame allocates nothing. */
struct number_text final {
    std::array<char, 32> buffer{};
    std::size_t size = 0;

    [[nodiscard]] std::string_view view() const { return {buffer.data(), size}; }
    [[nodiscard]] const char* c_str() const { return buffer.data(); }
};

template<class... Args>
[[nodiscard]] number_text format_number(std::format_string<Args...> pattern,
    Args&&... args) {
    number_text text;
    const auto written = std::format_to_n(text.buffer.data(),
        static_cast<std::ptrdiff_t>(text.buffer.size() - 1), pattern,
        std::forward<Args>(args)...);
    text.size = static_cast<std::size_t>(written.out - text.buffer.data());
    return text;
}

[[nodiscard]] number_text format_price(double value) {
    const double magnitude = std::fabs(value);
    const int decimals = magnitude >= 1000.0 ? 2
        : magnitude >= 100.0 ? 3
        : magnitude >= 10.0 ? 4
        : 5;
    return format_number("{:.{}f}", value, decimals);
}

/**
 * @brief The verdict the change pill wears.
 *
 * The direction, unless the data is too old to have one: a stale move is no
 * news, so it is neutral, and a delayed one wears the delay rather than the
 * direction, as the price tag on the plot does.
 */
[[nodiscard]] theme::tone change_tone(domain::data_health health,
    double change) {
    if (health == domain::data_health::stale)
        return theme::tone::neutral;
    if (health == domain::data_health::delayed)
        return theme::tone::warning;
    if (change > 0.0)
        return theme::tone::good;
    if (change < 0.0)
        return theme::tone::bad;
    return theme::tone::neutral;
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

/** @brief How a chart lays itself out, which follows from where it sits. */
enum class chart_layout { full, compact };

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

/** @brief The compact header's tooltip: what the full header spells out. */
void draw_compact_tooltip(const chart_summary& s) {
    if (!ImGui::BeginTooltip())
        return;
    const auto word = domain::data_health_word(s.health);
    ImGui::Text("%.*s", static_cast<int>(word.size()), word.data());
    if (!s.no_points)
        ImGui::Text("%zu points in view%s", s.plotted,
            s.span_ns == 0 ? ", every point in the series" : "");
    if (s.history.reading) {
        ImGui::TextDisabled("Reading older history...");
    } else if (s.short_shown_ns) {
        ImGui::TextDisabled("Shows %s of %s. %s",
            duration_text(*s.short_shown_ns).c_str(),
            duration_text(s.span_ns).c_str(), short_history_reason(s.history));
    }
    ImGui::TextDisabled("%s", "Right click the plot for period, style and EMA");
    ImGui::EndTooltip();
}

/**
 * @brief The compact header: the series, its period, and what it is worth, in
 * one tight row with the price at body size.
 *
 * @return Whether the period was clicked, which opens the view menu.
 */
[[nodiscard]] bool draw_compact_header(const chart_summary& s,
    int span_index) {
    ImGui::BeginGroup();
    widget::value_pill(s.label);
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
        chart_span_labels[static_cast<std::size_t>(span_index)]);
    const bool period_clicked = ImGui::IsItemClicked();
    if (!s.no_points) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        const auto price = format_price(s.last);
        ImGui::PushFont(icon::numeric_font());
        ImGui::TextUnformatted(price.view().data(),
            price.view().data() + price.view().size());
        ImGui::PopFont();
        ImGui::SameLine();
        widget::value_pill(format_number("{:+.2f}%", s.percent()).view(),
            change_tone(s.health, s.change()));
    }
    ImGui::EndGroup();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        draw_compact_tooltip(s);
    return period_clicked;
}

/** @brief One section of the view menu. @return Whether the choice changed. */
bool choose(const char* heading, std::span<const char* const> labels,
    int& selected) {
    ImGui::SeparatorText(heading);
    bool changed = false;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const int index = static_cast<int>(i);
        if (ImGui::MenuItem(labels[i], nullptr, selected == index)
            && selected != index) {
            selected = index;
            changed = true;
        }
    }
    return changed;
}

/**
 * @brief The compact chart's period, style and EMA, as a popup.
 *
 * The same choices the full chart's foot offers, on the same scratch, so a
 * tile undocked into a full chart shows what was chosen on the tile.
 *
 * @return Whether a choice changed.
 */
bool draw_view_menu(market_view_state& view) {
    if (!ImGui::BeginPopup("chart-view"))
        return false;
    bool changed = choose("Period", chart_span_labels, view.chart_span);
    changed |= choose("Style", chart_style_labels, view.chart_style);
    changed |= choose("Overlay", chart_ema_labels, view.chart_ema);
    ImGui::EndPopup();
    return changed;
}

/** @brief An icon at the right end of a chart's header, and what it asks. */
struct header_icon final {
    const char* id;
    const char* glyph;
    const char* tooltip;
    tile_request request;
};

constexpr std::array floating_icons{
    header_icon{"dock", icon::panel, "Dock into the desk", tile_request::dock},
};

constexpr std::array docked_icons{
    header_icon{"pop-out", icon::move, "Pop out into a floating window",
        tile_request::pop_out},
    header_icon{"close", icon::cancel, "Close this chart",
        tile_request::close},
};

/** @brief What the header's icons asked, and the width they keep. */
struct header_icons_drawn final {
    tile_request asked = tile_request::none;
    float width = 0.0f;
};

/**
 * @brief Draws @p icons at the right end of the header row, @p height tall,
 * and clips the rest of the row short of them.
 *
 * The icons come first so a narrow tile loses the end of its header text and
 * never a control. The caller draws the row and then pops the clip.
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

[[nodiscard]] header_icons_drawn begin_header_row(
    std::span<const header_icon> icons, float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float gap = style.ItemInnerSpacing.x;
    const float side = std::ceil(ImGui::GetFontSize() * 1.3f);
    const float width =
        static_cast<float>(icons.size()) * (side + gap) - gap;
    const float left = std::max(start.x,
        start.x + ImGui::GetContentRegionAvail().x - width);
    const float top = start.y + std::max(0.0f, std::floor((height - side) * 0.5f));

    header_icons_drawn drawn{.width = width + gap};
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const ImVec2 origin(left + static_cast<float>(i) * (side + gap), top);
        if (tile_icon(icons[i], origin, side))
            drawn.asked = icons[i].request;
    }

    ImGui::SetCursorScreenPos(start);
    ImGui::PushClipRect(start, ImVec2(left - gap, FLT_MAX), true);
    return drawn;
}

/** @brief Outlines the current window in the accent, the active tile's mark. */
void draw_focus_outline() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, max, false);
    draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f),
        ImVec2(max.x - 0.5f, max.y - 0.5f), theme::rail_selection_border, 0.0f,
        0, 1.0f);
    draw->PopClipRect();
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
    const auto layout = ImGui::IsWindowDocked() ? chart_layout::compact
                                                : chart_layout::full;

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
        draw_signed_out(input, status, foot);
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
    const double change = last - first;
    const ImU32 colour = change < 0.0 ? theme::failure_colour : up_colour;
    const ImVec4 line_colour = ImGui::ColorConvertU32ToFloat4(colour);

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
     * One row of chrome above the plot. Both layouts keep the foot; a compact
     * tile also has its settings a right click away.
     */
    std::optional<widget::foot_state> foot;
    bool open_view_menu = false;
    if (layout == chart_layout::full) {
        const auto icons = begin_header_row(floating_icons,
            ImGui::GetFrameHeight());
        state.tile = icons.asked;
        draw_full_header(summary);
        ImGui::SameLine();
        draw_series_flags(input, *session, chart->label, icons.width);
        ImGui::PopClipRect();
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ImGui::GetStyle().ItemSpacing.x, compact_row_gap));
        const auto icons = begin_header_row(docked_icons,
            widget::value_pill_size(summary.label).y);
        state.tile = icons.asked;
        open_view_menu = draw_compact_header(summary,
            state.market_view.chart_span);
        ImGui::SameLine();
        draw_series_flags(input, *session, chart->label, icons.width);
        ImGui::PopClipRect();
        ImGui::PopStyleVar();
    }

    if (row && !series.failure(*row).empty()) {
        const auto failure = series.failure(*row);
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%.*s", static_cast<int>(failure.size()), failure.data());
    }

    float foot_height = 0.0f;
    {
        foot = make_foot();
        set_health_foot(*foot, health, as_of_ns, input.wall_now_ns, reading);
        foot->controls.push_back(widget::foot_combo{
            .id = "period",
            .tooltip = "The span the chart plots",
            .options = chart_span_labels.data(),
            .option_count = static_cast<int>(chart_span_labels.size()),
            .selected = &state.market_view.chart_span});
        foot->controls.push_back(widget::foot_combo{
            .id = "style",
            .tooltip = "Draw candles or a line",
            .options = chart_style_labels.data(),
            .option_count = static_cast<int>(chart_style_labels.size()),
            .selected = &state.market_view.chart_style});
        foot->controls.push_back(widget::foot_combo{
            .id = "ema",
            .tooltip = "Overlay the exponential moving average of the window",
            .options = chart_ema_labels.data(),
            .option_count = static_cast<int>(chart_ema_labels.size()),
            .selected = &state.market_view.chart_ema});
        foot_height = widget::status_foot_height(status, *foot)
            + ImGui::GetStyle().ItemSpacing.y;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(available.y - foot_height, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##plot-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (reading) {
            ImGui::TextDisabled("Reading the series...");
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
                const double seconds = plotted > 1
                    ? xs[plotted - 1] - xs[0] : 0.0;
                const char* pattern = seconds <= 6.0 * 3600.0 ? "%H:%M:%S"
                    : seconds <= 90.0 * 24.0 * 3600.0 ? "%d %b %H:%M"
                    : "%d %b %Y";
                ImPlot::SetupAxisFormat(ImAxis_X1, format_time,
                    const_cast<char*>(pattern));
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");
                if (!use_candles) {
                    const double x_span = plotted > 1
                        ? xs[plotted - 1] - xs[0] : 0.0;
                    const double x_pad = x_span > 0.0
                        ? x_span * chart_end_padding_share
                        : chart_single_point_padding_s;
                    ImPlot::SetupAxisLimits(ImAxis_X1,
                        xs[0] - x_pad * chart_start_padding_share,
                        xs[plotted - 1] + x_pad,
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
                        static_cast<double>(plot.candle_width_ns) / 1.0e9;
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
                    fill_gradient_under_curve(xs, ys,
                        static_cast<int>(plotted),
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
            ImPlot::PopStyleColor();
            ImPlot::PopStyleVar(2);
        }
        if (layout == chart_layout::compact
            && ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
            && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            open_view_menu = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const auto before = state.market_view;
    if (widget::draw_status_foot(status, input.now_ns, *foot))
        chart_view_changed(handle, input, state, before);
    if (layout == chart_layout::compact) {
        if (open_view_menu)
            ImGui::OpenPopup("chart-view");
        if (draw_view_menu(state.market_view))
            chart_view_changed(handle, input, state, before);
        draw_focus_outline();
    }
    ImGui::PopID();
}

}
