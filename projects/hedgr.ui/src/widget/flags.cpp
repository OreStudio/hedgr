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
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include <algorithm>
#include <string>

namespace hedgr::ui::widget {

currency_pair currencies_of(std::string_view label) {
    const auto code = [](std::string_view word) {
        const bool iso = word.size() == 3
            && std::all_of(word.begin(), word.end(),
                [](char c) { return c >= 'A' && c <= 'Z'; });
        return iso ? std::string{word} : std::string{};
    };
    const auto second = [&label]() -> std::string_view {
        const auto first = label.find(' ');
        if (first == std::string_view::npos)
            return {};
        auto rest = label.substr(first + 1);
        const auto end = rest.find(' ');
        return rest.substr(0, end);
    }();

    if (second.empty())
        return {};
    const auto slash = second.find('/');
    if (slash != std::string_view::npos) {
        const auto base = second.substr(0, slash);
        const auto quote = second.substr(slash + 1);
        return {code(base), code(quote)};
    }
    return {code(second), {}};
}

float flag_width_for(float height) noexcept {
    return height * flag_aspect;
}

float flag_pair_width(float height, ImTextureID base,
    ImTextureID quote) noexcept {
    const int count = static_cast<int>(base != 0) + static_cast<int>(quote != 0);
    if (count == 0)
        return 0.0f;
    return flag_width_for(height) * static_cast<float>(count)
        + theme::flag_pair_gap * static_cast<float>(count - 1);
}

float draw_flag_pair(ImDrawList* draw, ImVec2 top_left, float height,
    ImTextureID base, ImTextureID quote) {
    const float width = flag_width_for(height);
    const ImU32 tint = IM_COL32(255, 255, 255, theme::flag_alpha);
    const ImU32 border =
        ImGui::ColorConvertFloat4ToU32(theme::card_border());
    float x = top_left.x;
    for (const auto texture : {base, quote}) {
        if (texture == 0)
            continue;
        const ImVec2 min(x, top_left.y);
        const ImVec2 max(x + width, top_left.y + height);
        draw->AddImageRounded(texture, min, max, ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f), tint, value_pill_rounding);
        draw->AddRect(min, max, border, value_pill_rounding, 0,
            value_pill_border);
        x += width + theme::flag_pair_gap;
    }
    return flag_pair_width(height, base, quote);
}

}
