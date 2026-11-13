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

namespace {

/** @brief @p word when it is an ISO-shaped currency code, or empty. */
[[nodiscard]] std::string iso_code(std::string_view word) {
    const bool iso = word.size() == 3
        && std::all_of(word.begin(), word.end(),
            [](char c) { return c >= 'A' && c <= 'Z'; });
    return iso ? std::string{word} : std::string{};
}

}

currency_pair currencies_of(
    const hedgr::model::domain::market_series_identity& identity) {
    /*
     * The subject is what the series is about, in the deployment's vocabulary:
     * a rate's currency, a spot's base, a default's reference entity. The
     * schema names the field, so the pair is read from the identity and never
     * from the characters of a name.
     */
    currency_pair pair;
    pair.base = iso_code(identity.scope);
    for (const auto& field : identity.fields) {
        if (field.name == "ccy") {
            pair.quote = iso_code(field.text);
            break;
        }
    }
    if (pair.base.empty())
        pair.quote.clear();
    return pair;
}

currency_pair currencies_of(std::string_view label) {
    /*
     * The label is the interface's reading of a series URI, and it says the
     * currency in different places for different assets: `FX GBP/USD` carries a
     * pair, `IR AUD 1D` names one currency beside the asset, and a curve's own
     * name carries it inside a hyphen - the window is titled `AUD-AONIA`, which
     * is one word and only its first half is a currency. So a word holding a
     * slash is the pair, and otherwise every part of a word, split at hyphens
     * and underscores, is looked at for the one currency.
     */
    currency_pair pair;
    auto word_start = std::size_t{0};
    for (std::size_t i = 0; i <= label.size(); ++i) {
        const bool end = i == label.size() || label[i] == ' ';
        if (!end)
            continue;
        const auto word = label.substr(word_start, i - word_start);
        word_start = i + 1;
        if (word.empty())
            continue;
        if (const auto slash = word.find('/');
            slash != std::string_view::npos) {
            return {iso_code(word.substr(0, slash)),
                iso_code(word.substr(slash + 1))};
        }
        auto part_start = std::size_t{0};
        for (std::size_t j = 0; j <= word.size(); ++j) {
            const bool part_end = j == word.size() || word[j] == '-'
                || word[j] == '_';
            if (!part_end)
                continue;
            const auto part = word.substr(part_start, j - part_start);
            part_start = j + 1;
            if (pair.base.empty())
                pair.base = iso_code(part);
        }
    }
    return pair;
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
