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
#include "hedgr.ui/render/surface_plot.hpp"
#include "hedgr.ui/render/time_text.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include "implot3d.h"
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
 * @brief Draws the surface on three axes.
 *
 * The surface is a surface, so it is drawn as one: strikes across, expiries
 * deep, volatility up. The library owns the projection, the lighting, the
 * colour scale and the rotation, which is what makes the same screen answer
 * "what shape is the smile" and "where is the wing" without a second view. A
 * hand-projected carpet could only ever be one fixed viewpoint, and its colour
 * legend said nothing about which way was up.
 *
 * The categories are the axes' own names - a strike is "25D", not 1.5 - so the
 * coordinates are grid positions and the labels travel as the ticks.
 */
void draw_surface(surface_plot& plot, const surface_plot_key& key,
    const model::state::surface_row& row,
    const model::state::surface_table& surfaces, ImVec2 size) {
    update_surface_plot(plot, key, row, surfaces);
    if (plot.x_count == 0 || plot.y_count == 0 || plot.zs.empty()) {
        ImGui::TextDisabled("The surface has no grid to draw.");
        return;
    }

    if (ImPlot3D::BeginPlot("##surface", size,
            ImPlot3DFlags_NoTitle | ImPlot3DFlags_NoLegend)) {
        ImPlot3D::SetupAxis(ImAxis3D_X, "Strike");
        ImPlot3D::SetupAxis(ImAxis3D_Y, "Expiry");
        ImPlot3D::SetupAxis(ImAxis3D_Z, "Volatility");
        ImPlot3D::SetupAxisTicks(ImAxis3D_X, plot.strike_at.data(),
            static_cast<int>(plot.strike_at.size()),
            plot.strike_labels.data());
        ImPlot3D::SetupAxisTicks(ImAxis3D_Y, plot.expiry_at.data(),
            static_cast<int>(plot.expiry_at.size()),
            plot.expiry_labels.data());
        ImPlot3D::PushColormap(ImPlot3DColormap_Viridis);
        ImPlot3D::PlotSurface("Volatility", plot.xs.data(), plot.ys.data(),
            plot.zs.data(), static_cast<int>(plot.x_count),
            static_cast<int>(plot.y_count), plot.low, plot.high);
        ImPlot3D::PopColormap();
        ImPlot3D::EndPlot();
    }
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
            draw_surface(state.surface_view,
                surface_plot_key{.version = found ? input.surfaces.version(*found) : 0,
                    .as_of_ns = row->as_of_ns},
                *row, input.surfaces, ImGui::GetContentRegionAvail());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    static_cast<void>(
        draw_foot_with_party(input, *session, status, foot, state));
    ImGui::PopID();
}

}
