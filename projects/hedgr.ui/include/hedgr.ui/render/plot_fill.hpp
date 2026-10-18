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
#ifndef HEDGR_UI_RENDER_PLOT_FILL_HPP
#define HEDGR_UI_RENDER_PLOT_FILL_HPP

#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cstdint>

namespace hedgr::ui::render {

/**
 * @brief Fills the area under a plotted line with a vertical colour gradient.
 *
 * ImPlot's own shaded fill is a flat colour. A gradient reads better under a
 * line: the line's colour at the curve, fading to nothing at the floor of the
 * plot, as a screen full of curves would otherwise be a block of colour. The
 * caller draws its line afterwards so the stroke stays crisp.
 *
 * Must be called between BeginPlot and EndPlot, after the axes are set up.
 *
 * @param xs x values, oldest first.
 * @param ys y values, index for index with @p xs.
 * @param count number of points.
 * @param colour line colour; its alpha is ignored.
 * @param top_alpha alpha at the curve, between zero and one.
 */
inline void fill_gradient_under_curve(const double* xs, const double* ys,
    int count, ImU32 colour, float top_alpha = 0.35f) {
    if (count < 2)
        return;
    auto* draw = ImPlot::GetPlotDrawList();
    if (draw == nullptr)
        return;

    const auto limits = ImPlot::GetPlotLimits();
    const float floor_y = ImPlot::PlotToPixels(0.0, limits.Y.Min).y;
    const auto alpha = static_cast<ImU32>(
        std::clamp(top_alpha, 0.0f, 1.0f) * 255.0f);
    const ImU32 top_colour = (colour & 0x00FFFFFFu) | (alpha << 24);
    const ImU32 bottom_colour = colour & 0x00FFFFFFu;
    const ImVec2 uv{0.0f, 0.0f};

    ImPlot::PushPlotClipRect();
    draw->PrimReserve((count - 1) * 6, (count - 1) * 6);
    for (int i = 0; i < count - 1; ++i) {
        const auto a = ImPlot::PlotToPixels(xs[i], ys[i]);
        const auto b = ImPlot::PlotToPixels(xs[i + 1], ys[i + 1]);
        const ImVec2 a_floor{a.x, floor_y};
        const ImVec2 b_floor{b.x, floor_y};

        draw->PrimVtx(a, uv, top_colour);
        draw->PrimVtx(b, uv, top_colour);
        draw->PrimVtx(b_floor, uv, bottom_colour);

        draw->PrimVtx(a, uv, top_colour);
        draw->PrimVtx(b_floor, uv, bottom_colour);
        draw->PrimVtx(a_floor, uv, bottom_colour);
    }
    ImPlot::PopPlotClipRect();
}

}

#endif
