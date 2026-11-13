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
#include "hedgr.ui/render/windows.hpp"

#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/market_status.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/party_foot.hpp"
#include "hedgr.ui/render/series_text.hpp"
#include "hedgr.ui/render/time_text.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

namespace theme = hedgr::ui::theme;
namespace model = hedgr::model;

/**
 * @brief One grid cell projected onto the screen.
 *
 * A surface has no third dimension in a window, so it is drawn as a carpet: the
 * strikes run left to right, the expiries run into the screen going up and
 * right, and the volatility is height. The projection is axonometric - no
 * perspective - because a reader comparing two heights is comparing the two
 * columns they sit on, and perspective would make the far corner shorter for no
 * reason a desk cares about.
 */
struct projection final {
    ImVec2 origin;
    float across = 0.0f;
    float depth_x = 0.0f;
    float depth_y = 0.0f;
    float height = 0.0f;
    float low = 0.0f;
    float span = 1.0f;

    /** @brief The screen point of one cell, @p u across and @p v deep at @p z. */
    [[nodiscard]] ImVec2 at(double u, double v, double z) const noexcept {
        const auto vertical = static_cast<float>((z - low) / span);
        return ImVec2(origin.x + across * static_cast<float>(u)
                + depth_x * static_cast<float>(v),
            origin.y - depth_y * static_cast<float>(v)
                - height * vertical);
    }
};

/** @brief The colour a volatility wears: low dark blue, high light violet. */
[[nodiscard]] ImU32 height_colour(const projection& p, double z) {
    const auto t = std::clamp(static_cast<float>((z - p.low) / p.span), 0.0f,
        1.0f);
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    ImGui::ColorConvertHSVtoRGB(0.62f + 0.16f * t, 0.62f,
        0.34f + 0.60f * t, red, green, blue);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, 1.0f));
}

/** @brief The volatility range the surface covers, and its shape. */
struct surface_shape final {
    double low = 0.0;
    double high = 0.0;
    bool usable = false;
};

[[nodiscard]] surface_shape shape_of(const model::state::surface_row& row) {
    surface_shape shape;
    if (!row.complete())
        return shape;
    shape.low = *std::min_element(row.values.begin(), row.values.end());
    shape.high = *std::max_element(row.values.begin(), row.values.end());
    shape.usable = std::isfinite(shape.low) && std::isfinite(shape.high);
    return shape;
}

/**
 * @brief Draws the surface's mesh and axes into the current child.
 *
 * Painter's algorithm: the farthest row is drawn first and the nearest last, so
 * a cell that stands up in front of another covers it. Sorting would be exact
 * for any projection; this one has a fixed depth order, which is what makes a
 * sort unnecessary.
 */
void draw_mesh(const model::state::surface_row& row,
    const model::state::surface_table& surfaces, ImVec2 size) {
    const auto& expiries = surfaces.axis(row.expiry_axis);
    const auto& strikes = surfaces.axis(row.strike_axis);
    const auto shape = shape_of(row);
    if (!shape.usable)
        return;

    /*
     * The carpet is laid out with a gutter on the right for the expiry names
     * and one under the front edge for the strikes. Without them the labels run
     * off a window that clips, and a name nobody can read is worse than none.
     */
    const ImVec2 top_left = ImGui::GetCursorScreenPos();
    const float usable_w = size.x * 0.80f;
    const float usable_h = size.y * 0.78f;

    projection p;
    p.low = static_cast<float>(shape.low);
    p.span = std::max(static_cast<float>(shape.high - shape.low), 1e-6f);
    p.across = usable_w * 0.60f;
    p.depth_x = usable_w * 0.34f;
    p.depth_y = usable_h * 0.30f;
    p.height = usable_h * 0.52f;
    p.origin = ImVec2(top_left.x + size.x * 0.05f,
        top_left.y + size.y * 0.80f);

    auto* draw = ImGui::GetWindowDrawList();
    const auto rows = row.rows;
    const auto columns = row.columns;
    const auto u_of = [columns](std::size_t column) {
        return columns <= 1 ? 0.0
                            : static_cast<double>(column)
                / static_cast<double>(columns - 1);
    };
    const auto v_of = [rows](std::size_t at) {
        return rows <= 1 ? 0.0
                         : static_cast<double>(at)
                / static_cast<double>(rows - 1);
    };

    /*
     * Far row first. The depth axis runs up the screen, so the last row is the
     * one behind everything and the first is the one in front.
     */
    for (std::size_t at = rows - 1; at-- > 0;) {
        for (std::size_t column = 0; column + 1 < columns; ++column) {
            const auto quad = std::array<ImVec2, 4>{
                p.at(u_of(column), v_of(at), row.at(at, column)),
                p.at(u_of(column + 1), v_of(at), row.at(at, column + 1)),
                p.at(u_of(column), v_of(at + 1), row.at(at + 1, column)),
                p.at(u_of(column + 1), v_of(at + 1),
                    row.at(at + 1, column + 1))};
            const auto mean = (row.at(at, column) + row.at(at, column + 1)
                    + row.at(at + 1, column) + row.at(at + 1, column + 1))
                / 4.0;
            /*
             * The cell is filled with its own height and edged in a dimmer
             * shade of the same colour, so the mesh reads as a surface rather
             * than as a heat map laid flat.
             */
            const ImVec2 cell[4]{quad[0], quad[1], quad[3], quad[2]};
            draw->AddConvexPolyFilled(cell, 4, height_colour(p, mean));
            draw->AddPolyline(cell, 4,
                ImGui::ColorConvertFloat4ToU32(theme::card_border()),
                ImDrawFlags_Closed, 1.0f);
        }
    }

    /*
     * The axes' names, drawn where their lines would run: the expiries along
     * the depth edge, the strikes along the front edge. A name every cell would
     * be unreadable at a dozen strikes, so only every other one is drawn when
     * they crowd.
     */
    ImGui::PushFont(hedgr::ui::icon::numeric_font());
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for (std::size_t at = 0; at < rows; ++at) {
        const auto point = p.at(1.0, v_of(at), shape.low);
        draw->AddText(ImVec2(point.x + 8.0f, point.y - 6.0f), text,
            expiries.labels[at]);
    }
    for (std::size_t column = 0; column < columns; ++column) {
        if (column % 2 != 0 && columns > 6)
            continue;
        const auto point = p.at(u_of(column), 0.0, shape.low);
        const auto width = ImGui::CalcTextSize(strikes.labels[column]).x;
        /*
         * Centred on its tick, but never off the edge: the first and last
         * names are the ones a centred label would clip.
         */
        const auto x = std::clamp(point.x - width * 0.5f, top_left.x,
            top_left.x + size.x - width);
        draw->AddText(ImVec2(x, point.y + 6.0f), text,
            strikes.labels[column]);
    }
    ImGui::PopFont();

    /* The range, at the top left, because a colour needs a legend. */
    ImGui::SetCursorScreenPos(
        ImVec2(p.origin.x, p.origin.y - p.depth_y - p.height - 18.0f));
    ImGui::TextDisabled("%s .. %s",
        format_price(shape.low).c_str(), format_price(shape.high).c_str());
    ImGui::SetItemTooltip("Volatility, low to high, over the grid below.");
}

}

void render_vol_surface(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    using hedgr::model::domain::vol_surface_state;

    ImGui::PushID("vol-surface");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* window = std::get_if<vol_surface_state>(&shape);
    if (window == nullptr) {
        widget::draw_view_state_body(status, foot,
            {widget::view_state::error, widget::view_words::read_failed,
                "This window does not hold a volatility surface."});
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

    const auto found = input.surfaces.find(
        input.sessions.instance_id(*session),
        input.sessions.account_id(*session), window->blueprint_id);
    const auto* row = found ? input.surfaces.get(*found) : nullptr;
    const bool reading = row != nullptr
        && row->state == model::state::surface_state::reading;
    const bool has_grid = row != nullptr && row->complete();

    auto facts = session_view_facts(input, handle);
    facts.reading = row == nullptr || reading;
    facts.read_failure = row != nullptr ? std::string_view{row->error}
                                        : std::string_view{};
    facts.has_data = has_grid;
    facts.empty_what = "This deployment publishes no surface for this blueprint.";
    const auto verdict = widget::view_state_of(facts);
    trace_view_state("volatility surface", window->label, state, verdict);

    set_health_foot(foot,
        hedgr::model::domain::assess_data_health(row != nullptr ? row->as_of_ns : 0,
            input.wall_now_ns, hedgr::model::domain::fx_health_thresholds),
        row != nullptr ? row->as_of_ns : 0, input.wall_now_ns, reading);
    widget::set_view_foot(foot, verdict);

    ImGui::AlignTextToFramePadding();
    widget::value_pill(window->label);
    if (!window->blueprint_id.empty())
        ImGui::SetItemTooltip("%s", window->blueprint_id.c_str());
    if (!window->base_currency.empty()) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Pair", "The currencies the surface is quoted on.");
        ImGui::SameLine();
        ImGui::Text("%s/%s", window->base_currency.c_str(),
            window->quote_currency.c_str());
    }
    if (has_grid) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        widget::caption("Grid", "Expiries by strikes.");
        ImGui::SameLine();
        ImGui::Text("%zu by %zu", row->rows, row->columns);
    }
    ImGui::SameLine();
    /*
     * The surface's currency is the pair's *base*: the blueprint states two,
     * and the flag that stands for the instrument is the one it is quoted from.
     */
    draw_series_flags(input, *session, {window->base_currency, window->quote_currency});

    const float foot_height = widget::status_foot_height(status, foot);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float body_height = std::max(
        available.y - foot_height - ImGui::GetStyle().ItemSpacing.y, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##surface-area", ImVec2(available.x, body_height),
            ImGuiChildFlags_None)) {
        if (!has_grid) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 room = ImGui::GetContentRegionAvail();
            widget::draw_view_state(verdict, at,
                ImVec2(at.x + room.x, at.y + room.y),
                theme::view_state_glyph_size);
        } else {
            draw_mesh(*row, input.surfaces, ImGui::GetContentRegionAvail());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    static_cast<void>(
        draw_foot_with_party(input, *session, status, foot, state));
    ImGui::PopID();
}

}
