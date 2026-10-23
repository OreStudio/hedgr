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
#ifndef HEDGR_UI_RENDER_SERIES_TEXT_HPP
#define HEDGR_UI_RENDER_SERIES_TEXT_HPP

#include "hedgr.model/domain/health.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace hedgr::ui::render {

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

/**
 * @brief A price, with the decimals its magnitude needs.
 *
 * An FX pair quotes to five places and a rate to two, and the plot has no way
 * to ask which is which: the magnitude answers, and the alternative is a label
 * reading "1.23456" beside one reading "0.00000". A chart and a card read the
 * same price, so they share this.
 */
[[nodiscard]] inline number_text format_price(double value) {
    const double magnitude = std::fabs(value);
    const int decimals = magnitude >= 1000.0 ? 2
        : magnitude >= 100.0 ? 3
        : magnitude >= 10.0 ? 4
        : 5;
    return format_number("{:.{}f}", value, decimals);
}

/**
 * @brief The verdict a change pill wears.
 *
 * The direction, unless the data is too old to have one: a stale move is no
 * news, so it is neutral, and a delayed one wears the delay rather than the
 * direction, as the price tag on the plot does.
 */
[[nodiscard]] inline theme::tone change_tone(
    hedgr::model::domain::data_health health, double change) {
    if (health == hedgr::model::domain::data_health::stale)
        return theme::tone::neutral;
    if (health == hedgr::model::domain::data_health::delayed)
        return theme::tone::warning;
    if (change > 0.0)
        return theme::tone::good;
    if (change < 0.0)
        return theme::tone::bad;
    return theme::tone::neutral;
}

}

#endif
