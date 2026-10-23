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
#include "hedgr.ui/render/mini_plot.hpp"

#include "hedgr.model/domain/health.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/market_view.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/series_text.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/plot_state.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

namespace domain = hedgr::model::domain;
namespace state = hedgr::model::state;

/** @brief Where a card's rows sit, from the tile and the font. */
struct card_layout final {
    ImVec2 min;
    ImVec2 max;
    float gap = 0.0f;
    float header = 0.0f;
    float price = 0.0f;
    float foot = 0.0f;
    bool stacked_foot = false;
    ImVec2 line_min;
    ImVec2 line_max;
};

[[nodiscard]] card_layout lay_out(ImVec2 origin, ImVec2 size, float header,
    bool with_price) {
    const float pad = std::clamp(
        std::min(size.x, size.y) * theme::mini_plot_padding_share,
        theme::mini_plot_padding_min, theme::mini_plot_padding_max);
    card_layout l;
    l.min = ImVec2(origin.x + pad, origin.y + pad);
    l.max = ImVec2(origin.x + size.x - pad, origin.y + size.y - pad);
    l.gap = std::round(ImGui::GetFontSize() * theme::mini_plot_gap_share);
    l.header = header;
    l.price = with_price ? widget::headline_height() : 0.0f;

    const float least = size.y * theme::mini_plot_line_share;
    const float above = l.header + (with_price ? l.gap + l.price : 0.0f);
    const float stacked = widget::labelled_value_height();
    const float inline_row = widget::value_pill_size("0").y;
    const float height = l.max.y - l.min.y;
    l.stacked_foot = height - above - 2.0f * l.gap - stacked >= least;
    l.foot = l.stacked_foot ? stacked : inline_row;

    /*
     * The line always ends above the foot row. When the rows above leave less
     * than a half-height line, it rises behind the price instead of dropping
     * onto the pill. A card too small for any foot row clips it.
     */
    const float bottom = std::max(l.max.y - l.foot - l.gap, l.min.y + 4.0f);
    const float top = std::min(l.min.y + above + l.gap, bottom - least * 0.5f);
    l.line_min = ImVec2(l.min.x, std::max(l.min.y, top));
    l.line_max = ImVec2(l.max.x, bottom);
    return l;
}

/** @brief Draws the flags and the pair chip, and returns the row's height. */
void draw_header(const render_input& input,
    std::optional<state::session_handle> session, std::string_view label,
    ImVec2 at, float width) {
    const float pill_h = widget::value_pill_size(label).y;
    float chip_x = at.x;
    if (session) {
        request_flags(input, *session, label);
        const auto pair = widget::currencies_of(label);
        const auto base = image_texture(input, *session, pair.base);
        const auto quote = image_texture(input, *session, pair.quote);
        const float flag_h = pill_h * theme::flag_height_fraction;
        const float flags_w = widget::flag_pair_width(flag_h, base, quote);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        if (flags_w > 0.0f
            && flags_w + gap + widget::value_pill_size(label).x <= width) {
            widget::draw_flag_pair(ImGui::GetWindowDrawList(),
                ImVec2(at.x, at.y + (pill_h - flag_h) * 0.5f), flag_h, base,
                quote);
            chip_x += flags_w + gap;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(chip_x, at.y));
    widget::value_pill(label);
}

void dim_line(ImVec2 at, const char* text) {
    ImGui::SetCursorScreenPos(at);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::dim_text());
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

}

namespace {

[[nodiscard]] bool is_taken(std::span<const card_cell> occupied,
    card_cell cell) noexcept {
    return std::ranges::find(occupied, cell) != occupied.end();
}

}

card_cell cell_for(const card_grid& grid, ImVec2 position) noexcept {
    const auto nearest = [](float offset, float pitch, int count) {
        const int index = static_cast<int>(std::lround(offset / pitch));
        return std::clamp(index, 0, std::max(count, 1) - 1);
    };
    return card_cell{
        nearest(position.x - grid.origin.x, grid.pitch.x, grid.columns),
        nearest(position.y - grid.origin.y, grid.pitch.y, grid.rows)};
}

ImVec2 cell_position(const card_grid& grid, card_cell cell) noexcept {
    return ImVec2(grid.origin.x + static_cast<float>(cell.column) * grid.pitch.x,
        grid.origin.y + static_cast<float>(cell.row) * grid.pitch.y);
}

card_cell snap_cell(const card_grid& grid, ImVec2 position,
    std::span<const card_cell> occupied) noexcept {
    const card_cell own = cell_for(grid, position);
    if (!is_taken(occupied, own))
        return own;
    card_cell best = own;
    float best_distance = 0.0f;
    bool found = false;
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            const card_cell cell{column, row};
            if (is_taken(occupied, cell))
                continue;
            const ImVec2 at = cell_position(grid, cell);
            const float dx = at.x - position.x;
            const float dy = at.y - position.y;
            const float distance = dx * dx + dy * dy;
            if (!found || distance < best_distance) {
                best = cell;
                best_distance = distance;
                found = true;
            }
        }
    }
    return best;
}

ImVec2 snap_position(const card_grid& grid, ImVec2 position,
    std::span<const card_cell> occupied) noexcept {
    return cell_position(grid, snap_cell(grid, position, occupied));
}

card_cell first_free_cell(const card_grid& grid,
    std::span<const card_cell> occupied) noexcept {
    const int columns = std::max(grid.columns, 1);
    for (int index = 0;; ++index) {
        const card_cell cell{index % columns, index / columns};
        if (!is_taken(occupied, cell))
            return cell;
    }
}

std::int64_t mini_plot_span(const render_input& input, window_handle card) {
    const auto held = input.windows.series(card);
    const auto& shape = input.windows.state(card);
    const auto* own = std::get_if<domain::mini_plot_state>(&shape);
    const std::int64_t opened_with = own != nullptr
        ? own->span_ns : chart_span_ns(input.market_view.chart_span);
    if (held.empty())
        return opened_with;

    const auto& layout = input.windows.layout();
    const auto kinds = input.windows.kind_column();
    const auto open = input.windows.open_column();
    for (std::size_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0 || open[index] == 0
            || static_cast<domain::window_kind>(kinds[index])
                != domain::window_kind::chart)
            continue;
        const window_handle chart{static_cast<std::uint32_t>(index),
            layout.generation[index]};
        const auto* view = std::get_if<domain::chart_state>(
            &input.windows.state(chart));
        const auto plotted = input.windows.series(chart);
        if (view != nullptr && view->view && !plotted.empty()
            && plotted.front() == held.front())
            return view->view->span_ns;
    }
    return opened_with;
}

std::size_t mini_plot_first(const state::series_columns& points,
    std::int64_t span_ns) noexcept {
    if (points.size < 2 || span_ns <= 0)
        return 0;
    const auto cutoff = points.at_ns[points.size - 1] - span_ns;
    const auto* const found = std::lower_bound(points.at_ns,
        points.at_ns + points.size - 1, cutoff);
    return static_cast<std::size_t>(found - points.at_ns);
}

sparkline make_sparkline(const state::series_columns& points,
    std::size_t first) noexcept {
    sparkline line;
    if (first >= points.size || points.size - first < 2)
        return line;
    const std::size_t count = points.size - first;
    const std::size_t n = std::min(count, theme::mini_plot_point_budget);
    const std::int64_t* const at = points.at_ns + first;
    const std::int64_t begin = at[0];
    const std::int64_t span = at[count - 1] - begin;

    double low = 0.0;
    double high = 0.0;
    std::array<double, theme::mini_plot_point_budget> picked{};
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t index = k;
        if (count > n) {
            const auto t = begin + static_cast<std::int64_t>(
                static_cast<double>(span) * static_cast<double>(k)
                / static_cast<double>(n - 1));
            const auto* const after = std::upper_bound(at, at + count, t);
            index = static_cast<std::size_t>(after - at) - 1;
        }
        const double v = points.value[first + index];
        picked[k] = v;
        low = k == 0 ? v : std::min(low, v);
        high = k == 0 ? v : std::max(high, v);
        line.x[k] = count > n || span <= 0
            ? static_cast<float>(k) / static_cast<float>(n - 1)
            : static_cast<float>(at[index] - begin)
                / static_cast<float>(span);
    }
    for (std::size_t k = 0; k < n; ++k)
        line.y[k] = high > low
            ? static_cast<float>((picked[k] - low) / (high - low))
            : 0.5f;
    line.size = n;
    return line;
}

void draw_sparkline(ImDrawList* draw, const sparkline& line, ImVec2 min,
    ImVec2 max, ImU32 colour) {
    if (line.size < 2 || max.x <= min.x || max.y <= min.y)
        return;
    const float inset = theme::plot_line_weight;
    const float top = min.y + inset;
    const float floor_y = max.y;
    const float height = std::max(floor_y - inset - top, 0.0f);

    std::array<ImVec2, theme::mini_plot_point_budget> path;
    for (std::size_t i = 0; i < line.size; ++i)
        path[i] = ImVec2(min.x + line.x[i] * (max.x - min.x),
            floor_y - inset - line.y[i] * height);

    const auto alpha = static_cast<ImU32>(
        std::clamp(theme::mini_plot_fill_alpha, 0.0f, 1.0f) * 255.0f);
    const ImU32 top_colour = (colour & 0x00FFFFFFu) | (alpha << 24);
    const ImU32 bottom_colour = colour & 0x00FFFFFFu;
    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    const int quads = static_cast<int>(line.size) - 1;
    draw->PrimReserve(quads * 6, quads * 6);
    for (int i = 0; i < quads; ++i) {
        const ImVec2 a = path[static_cast<std::size_t>(i)];
        const ImVec2 b = path[static_cast<std::size_t>(i) + 1];
        const ImVec2 a_floor(a.x, floor_y);
        const ImVec2 b_floor(b.x, floor_y);
        draw->PrimVtx(a, uv, top_colour);
        draw->PrimVtx(b, uv, top_colour);
        draw->PrimVtx(b_floor, uv, bottom_colour);
        draw->PrimVtx(a, uv, top_colour);
        draw->PrimVtx(b_floor, uv, bottom_colour);
        draw->PrimVtx(a_floor, uv, bottom_colour);
    }
    draw->AddPolyline(path.data(), static_cast<int>(line.size),
        colour | 0xFF000000u, ImDrawFlags_None, theme::plot_line_weight);
}

void render_mini_plot(window_handle handle, const render_input& input,
    window_ui_state& state, hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(status);
    static_cast<void>(lock);

    const auto& shape = input.windows.state(handle);
    const auto* card = std::get_if<domain::mini_plot_state>(&shape);
    if (card == nullptr)
        return;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f || size.y < 1.0f)
        return;
    const ImVec2 corner(origin.x + size.x, origin.y + size.y);
    auto* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, corner,
        ImGui::ColorConvertFloat4ToU32(theme::card_surface()),
        theme::mini_plot_rounding);
    draw->AddRect(origin, corner,
        ImGui::ColorConvertFloat4ToU32(theme::card_border()),
        theme::mini_plot_rounding);

    const auto held = input.windows.series(handle);
    const bool is_held = !held.empty();
    const bool has_row = is_held && input.market.contains(held.front());
    const auto row = has_row ? std::optional{held.front()} : std::nullopt;
    const bool reading = !row
        || (input.market.observed_at_ns(*row) == 0
            && input.market.failure(*row).empty());
    const std::int64_t span_ns = mini_plot_span(input, handle);
    const state::series_columns points = row
        ? input.market.columns(*row, domain::market_level_for_span(span_ns))
        : state::series_columns{};
    const bool has_points = !reading && points.size > 0;

    const std::string_view label = card->label;
    const float header = widget::value_pill_size(label).y;
    const card_layout l = lay_out(origin, size, header, has_points);

    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    const ImVec2 extent = window->DC.CursorMaxPos;
    ImGui::PushClipRect(ImVec2(origin.x + 1.0f, origin.y + 1.0f),
        ImVec2(corner.x - 1.0f, corner.y - 1.0f), true);

    const auto session = signed_in_session(input, handle);
    const auto bound = input.windows.session_of(handle);
    const widget::plot_state card_state = widget::plot_state_for(
        bound ? input.sessions.status(*bound)
              : domain::session_status::disconnected,
        reading);
    const std::size_t first = has_points
        ? mini_plot_first(points, span_ns) : 0;
    const double open = has_points ? points.value[first] : 0.0;
    const double last = has_points ? points.value[points.size - 1] : 0.0;
    const auto health = domain::assess_data_health(
        row ? input.market.newest_at_ns(*row) : 0, input.wall_now_ns,
        domain::fx_health_thresholds);
    const theme::tone tone = change_tone(health, last - open);

    /*
     * The line and its fill are the chart's, whatever the series did. Only the
     * change pill carries the verdict.
     */
    if (has_points)
        draw_sparkline(draw, make_sparkline(points, first), l.line_min,
            l.line_max,
            ImGui::ColorConvertFloat4ToU32(theme::chart_line_colour()));

    draw_header(input, session, label, l.min, l.max.x - l.min.x);
    const float below_header = l.min.y + l.header + l.gap;
    if (has_points) {
        ImGui::SetCursorScreenPos(ImVec2(l.min.x, below_header));
        widget::headline_value(format_price(last).view());

        const double percent = open != 0.0 ? (last - open) / open * 100.0 : 0.0;
        const auto change = format_number("{:+.2f}%", percent);
        const float foot_top = l.max.y - l.foot;
        ImGui::SetCursorScreenPos(ImVec2(l.min.x, foot_top));
        widget::caption(chart_spans[static_cast<std::size_t>(
            chart_span_index(span_ns))].label);
        if (l.stacked_foot) {
            ImGui::SetCursorScreenPos(ImVec2(l.min.x,
                foot_top + widget::labelled_value_height()
                    - widget::value_pill_size(change.view()).y));
        } else {
            ImGui::SameLine();
        }
        widget::value_pill(change.view(), tone);
    } else if (!is_held) {
        dim_line(ImVec2(l.min.x, below_header), "Series not held");
    } else if (card_state != widget::plot_state::none) {
        /* A card is too small for words, so the glyph alone says it. */
        const ImVec2 area_min(l.min.x, below_header);
        const float glyph = std::min({theme::plot_state_card_glyph_size,
            l.max.x - l.min.x, l.max.y - below_header});
        widget::draw_plot_state(card_state, area_min, l.max, glyph, false);
    } else if (!input.market.failure(*row).empty()) {
        const auto failure = input.market.failure(*row);
        ImGui::SetCursorScreenPos(ImVec2(l.min.x, below_header));
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour), "%.*s",
            static_cast<int>(failure.size()), failure.data());
    } else {
        dim_line(ImVec2(l.min.x, below_header), "No points yet");
    }

    ImGui::PopClipRect();

    /*
     * The close control shows only while the pointer is over the card. It is
     * a button, so a press on it never reaches the window to move it, and a
     * release on it is not a click on the tile. The release frame still has
     * the button active, so hover is read through that block, or the button
     * would vanish before it could report its click.
     */
    const bool hovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const float side = theme::mini_plot_close_side;
    const ImVec2 close_min(corner.x - side - theme::mini_plot_grid_gap,
        origin.y + theme::mini_plot_grid_gap);
    bool over_close = false;
    if (hovered) {
        ImGui::SetCursorScreenPos(close_min);
        if (ImGui::InvisibleButton("##close", ImVec2(side, side)))
            state.tile = tile_request::close;
        over_close = ImGui::IsItemHovered();
        ImGui::SetItemTooltip("Close");
        const ImVec2 close_max(close_min.x + side, close_min.y + side);
        if (over_close)
            draw->AddRectFilled(close_min, close_max,
                ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                ImGui::GetStyle().FrameRounding);
        const ImVec2 glyph = ImGui::CalcTextSize(icon::cancel);
        draw->AddText(ImVec2(close_min.x + std::floor((side - glyph.x) * 0.5f),
                           close_min.y + std::floor((side - glyph.y) * 0.5f)),
            ImGui::GetColorU32(over_close ? ImGuiCol_Text : ImGuiCol_TextDisabled),
            icon::cancel);
    }
    window->DC.CursorMaxPos = extent;
    /*
     * The tile is not a button: a card has no title bar, so a press on its body
     * has to reach ImGui as a press on the window, which moves the card. A
     * click is then a release over the window that never travelled past the
     * drag threshold.
     */
    if (hovered)
        ImGui::SetMouseCursor(over_close ? ImGuiMouseCursor_Arrow
                                         : ImGuiMouseCursor_Hand);
    const bool clicked = hovered && !over_close
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
    if (clicked && session) {
        request_series_window(input, *session, domain::window_kind::chart,
            card->series_id, card->label);
    }
}

}
