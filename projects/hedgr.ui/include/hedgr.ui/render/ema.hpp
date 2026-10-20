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
#ifndef HEDGR_UI_RENDER_EMA_HPP
#define HEDGR_UI_RENDER_EMA_HPP

#include <cstddef>
#include <span>
#include <vector>

namespace hedgr::ui::render {

/** @brief The period the chart's moving average uses, as the web chart does. */
inline constexpr std::size_t ema_period = 20;

/**
 * @brief The exponential moving average of a series, one value per input.
 *
 * Seeded with the first value: each next value moves 2/(period+1) of the way to
 * the new one, so the line starts where the series starts rather than at zero.
 * It replaces what @p result held and reuses its storage.
 */
inline void exponential_moving_average(std::span<const double> values,
    std::vector<double>& result, std::size_t period = ema_period) {
    result.clear();
    result.reserve(values.size());
    const double alpha = 2.0 / static_cast<double>(period + 1);
    for (const double value : values) {
        result.push_back(result.empty()
                ? value
                : result.back() + alpha * (value - result.back()));
    }
}

}

#endif
