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
#ifndef HEDGR_UI_RENDER_MARKET_VIEW_HPP
#define HEDGR_UI_RENDER_MARKET_VIEW_HPP

#include "hedgr.ui/export.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace hedgr::ui::render {

/** @brief One span a live chart can be cropped to. */
struct chart_span final {
    const char* label;
    std::int64_t ns;
};

/** @brief The spans the period combo offers, in the order it lists them. */
inline constexpr std::array<chart_span, 6> chart_spans{{
    {"1M", std::int64_t{1} * 60 * 1'000'000'000},
    {"5M", std::int64_t{5} * 60 * 1'000'000'000},
    {"15M", std::int64_t{15} * 60 * 1'000'000'000},
    {"1H", std::int64_t{60} * 60 * 1'000'000'000},
    {"1D", std::int64_t{24} * 60 * 60 * 1'000'000'000},
    {"All", 0},
}};

/** @brief The period combo's labels, index for index with @c chart_spans. */
inline constexpr std::array<const char*, 6> chart_span_labels{
    "1M", "5M", "15M", "1H", "1D", "All"};

/** @brief The chart styles the style combo offers. */
inline constexpr std::array<const char*, 2> chart_style_labels{
    "Candles", "Line"};

/** @brief The EMA choices the overlay combo offers. */
inline constexpr std::array<const char*, 2> chart_ema_labels{
    "EMA off", "EMA 20"};

/** @brief The curve counts the IR history combo offers, one to ten. */
inline constexpr std::array<const char*, 10> term_history_labels{
    "1 curve", "2 curves", "3 curves", "4 curves", "5 curves", "6 curves",
    "7 curves", "8 curves", "9 curves", "10 curves"};

/** @brief The smallest number of curves the history overlay keeps. */
inline constexpr int term_history_min = 1;

/** @brief The largest number of curves the history overlay keeps. */
inline constexpr int term_history_max = 10;

/**
 * @brief The market-data choices that outlive the window that made them.
 *
 * A reader who chooses candles and a one-hour period is telling the interface
 * how they read a chart, not describing one window. The next chart opens with
 * the same choices, and the file this state is written to keeps them across a
 * restart.
 */
struct market_view_state final {
    /** @brief The selected span, as an index into @c chart_spans. */
    int chart_span = 1;

    /** @brief The selected style, as an index into @c chart_style_labels. */
    int chart_style = 1;

    /** @brief The selected EMA overlay, as an index into @c chart_ema_labels. */
    int chart_ema = 0;

    /** @brief How many recent curves an IR window overlays. */
    int term_history = 5;
};

/** @brief The nanoseconds a span index names, or zero for the whole series. */
[[nodiscard]] inline std::int64_t chart_span_ns(int index) {
    if (index < 0 || index >= static_cast<int>(chart_spans.size()))
        return chart_spans[1].ns;
    return chart_spans[static_cast<std::size_t>(index)].ns;
}

/** @brief The index of the span that names @p ns, or the default. */
[[nodiscard]] inline int chart_span_index(std::int64_t ns) {
    for (std::size_t i = 0; i < chart_spans.size(); ++i) {
        if (chart_spans[i].ns == ns)
            return static_cast<int>(i);
    }
    return 1;
}

/**
 * @brief Reads market view preferences into @p state.
 *
 * Unknown keys and unusable values are left alone, so a file written by a newer
 * build still opens with every choice this build understands. The function is
 * total: a missing or damaged file is a first run, not an error a reader has to
 * see.
 */
HEDGR_UI_EXPORT void parse_market_view(std::string_view text,
    market_view_state& state);

/** @brief Writes @p state in the text form @c parse_market_view reads. */
[[nodiscard]] HEDGR_UI_EXPORT std::string
serialize_market_view(const market_view_state& state);

/**
 * @brief Reads the preferences file at @p path, leaving @p state on failure.
 *
 * @return whether a file was read and parsed.
 */
HEDGR_UI_EXPORT bool load_market_view(const std::filesystem::path& path,
    market_view_state& state);

/** @brief Writes @p state to @p path, creating its directory if needed. */
HEDGR_UI_EXPORT bool save_market_view(const std::filesystem::path& path,
    const market_view_state& state);

}

#endif
