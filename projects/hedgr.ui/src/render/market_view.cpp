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
#include "hedgr.ui/render/market_view.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hedgr::ui::render {

namespace {

[[nodiscard]] std::string_view trim(std::string_view text) {
    while (!text.empty()
        && (text.front() == ' ' || text.front() == '\t'
            || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty()
        && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

[[nodiscard]] bool read_int(std::string_view text, int& value) {
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] int style_index(std::string_view text) {
    if (text == "candles")
        return 0;
    if (text == "line")
        return 1;
    return -1;
}

[[nodiscard]] std::string_view style_name(int index) {
    return index == 0 ? "candles" : "line";
}

}

void parse_market_view(std::string_view text, market_view_state& state) {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        const auto line = trim(text.substr(at,
            end == std::string_view::npos ? text.size() - at : end - at));
        at = end == std::string_view::npos ? text.size() : end + 1;
        if (line.empty() || line.front() == '#')
            continue;

        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
            continue;
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));

        if (key == "period") {
            for (std::size_t i = 0; i < chart_spans.size(); ++i) {
                if (value == chart_spans[i].token
                    || value == chart_spans[i].legacy_token)
                    state.chart_span = static_cast<int>(i);
            }
        } else if (key == "style") {
            const auto index = style_index(value);
            if (index >= 0)
                state.chart_style = index;
        } else if (key == "ema") {
            if (value == "on")
                state.chart_ema = 1;
            else if (value == "off")
                state.chart_ema = 0;
        } else if (key == "toolbar") {
            if (value == "on")
                state.toolbar_visible = true;
            else if (value == "off")
                state.toolbar_visible = false;
        } else if (key == "history") {
            int history = 0;
            if (read_int(value, history))
                state.term_history = std::clamp(
                    history, term_history_min, term_history_max);
        }
    }
}

std::string serialize_market_view(const market_view_state& state) {
    const auto span = state.chart_span >= 0
            && state.chart_span < static_cast<int>(chart_spans.size())
        ? state.chart_span
        : 1;
    const auto style = state.chart_style >= 0
            && state.chart_style < static_cast<int>(chart_style_labels.size())
        ? state.chart_style
        : 1;
    const auto history = std::clamp(state.term_history, term_history_min,
        term_history_max);

    std::string text;
    text += "period=";
    text += chart_spans[static_cast<std::size_t>(span)].token;
    text += "\nstyle=";
    text += style_name(style);
    text += "\nema=";
    text += state.chart_ema == 0 ? "off" : "on";
    text += "\nhistory=";
    text += std::to_string(history);
    text += "\ntoolbar=";
    text += state.toolbar_visible ? "on" : "off";
    text += '\n';
    return text;
}

bool load_market_view(const std::filesystem::path& path,
    market_view_state& state) {
    std::ifstream input(path);
    if (!input)
        return false;

    const std::string text{(std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()};
    auto parsed = state;
    parse_market_view(text, parsed);
    state = parsed;
    return true;
}

bool save_market_view(const std::filesystem::path& path,
    const market_view_state& state) {
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;

    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;
    output << serialize_market_view(state);
    return static_cast<bool>(output);
}

}
