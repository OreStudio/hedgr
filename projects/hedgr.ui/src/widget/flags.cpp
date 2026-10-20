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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace hedgr::ui::widget {

namespace {

constexpr ImU32 red = IM_COL32(200, 16, 46, 255);
constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
constexpr ImU32 blue = IM_COL32(0, 40, 104, 255);
constexpr ImU32 navy = IM_COL32(0, 32, 91, 255);
constexpr ImU32 yellow = IM_COL32(255, 206, 0, 255);
constexpr ImU32 green = IM_COL32(0, 122, 61, 255);
constexpr ImU32 black = IM_COL32(0, 0, 0, 255);
constexpr ImU32 grey = IM_COL32(110, 110, 110, 255);
constexpr ImU32 saffron = IM_COL32(255, 153, 51, 255);
constexpr ImU32 brown = IM_COL32(139, 90, 43, 255);

constexpr std::array<std::string_view, 27> known_currencies{
    "AUD", "BRL", "CAD", "CHF", "CLP", "CNY", "COP", "CZK", "DKK",
    "EUR", "GBP", "HKD", "HUF", "IDR", "ILS", "INR", "JPY", "KRW",
    "MXN", "NOK", "NZD", "PLN", "SEK", "SGD", "TRY", "USD", "ZAR"};

}

bool has_flag(std::string_view currency) {
    for (const auto code : known_currencies) {
        if (code == currency)
            return true;
    }
    return false;
}

bool flag_available(std::string_view currency, ImTextureID texture) {
    return !currency.empty() && (texture != 0 || has_flag(currency));
}

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

void draw_flag(ImDrawList* draw, ImVec2 p, std::string_view currency,
    float scale, ImTextureID texture) {
    const float width = flag_width * scale;
    const float height = flag_height * scale;
    if (texture != 0) {
        draw->AddImage(texture, p, ImVec2(p.x + width, p.y + height));
        return;
    }
    /*
     * The artwork is written in a sixteen by eleven unit space and scaled to
     * whatever the widget asks for, so a larger flag is the same drawing and
     * not a differently proportioned one.
     */
    const float sx = width / 16.0f;
    const float sy = height / 11.0f;
    const float sr = std::min(sx, sy);
    const auto X = [&](float v) { return p.x + v * sx; };
    const auto Y = [&](float v) { return p.y + v * sy; };
    const auto R = [&](float v) { return v * sr; };
    const auto rect = [&](float x0, float y0, float x1, float y1, ImU32 c) {
        draw->AddRectFilled(ImVec2(X(x0), Y(y0)), ImVec2(X(x1), Y(y1)), c);
    };
    const auto disc = [&](float cx, float cy, float r, ImU32 c) {
        draw->AddCircleFilled(ImVec2(X(cx), Y(cy)), R(r), c);
    };
    const auto ring = [&](float cx, float cy, float r, ImU32 c, float t) {
        draw->AddCircle(ImVec2(X(cx), Y(cy)), R(r), c, 16, t * sr);
    };
    const auto line = [&](float x0, float y0, float x1, float y1, ImU32 c,
                          float t) {
        draw->AddLine(ImVec2(X(x0), Y(y0)), ImVec2(X(x1), Y(y1)), c, t * sr);
    };
    const auto tri = [&](float x0, float y0, float x1, float y1, float x2,
                         float y2, ImU32 c) {
        draw->AddTriangleFilled(ImVec2(X(x0), Y(y0)), ImVec2(X(x1), Y(y1)),
            ImVec2(X(x2), Y(y2)), c);
    };
    const auto quad = [&](float x0, float y0, float x1, float y1, float x2,
                          float y2, float x3, float y3, ImU32 c) {
        draw->AddQuadFilled(ImVec2(X(x0), Y(y0)), ImVec2(X(x1), Y(y1)),
            ImVec2(X(x2), Y(y2)), ImVec2(X(x3), Y(y3)), c);
    };
    const auto bars_h = [&](std::initializer_list<ImU32> colours) {
        const float band = 11.0f / static_cast<float>(colours.size());
        float y = 0.0f;
        for (const auto c : colours) {
            rect(0.0f, y, 16.0f, y + band, c);
            y += band;
        }
    };
    const auto bars_v = [&](std::initializer_list<ImU32> colours) {
        const float band = 16.0f / static_cast<float>(colours.size());
        float x = 0.0f;
        for (const auto c : colours) {
            rect(x, 0.0f, x + band, 11.0f, c);
            x += band;
        }
    };
    const auto nordic = [&](ImU32 field, ImU32 cross, bool inner) {
        rect(0.0f, 0.0f, 16.0f, 11.0f, field);
        rect(5.76f - 1.5f, 0.0f, 5.76f + 1.5f, 11.0f, cross);
        rect(0.0f, 5.5f - 1.5f, 16.0f, 5.5f + 1.5f, cross);
        if (inner) {
            rect(5.76f - 0.5f, 0.0f, 5.76f + 0.5f, 11.0f, blue);
            rect(0.0f, 5.5f - 0.5f, 16.0f, 5.5f + 0.5f, blue);
        }
    };
    const auto canton_union = [&]() {
        rect(0.0f, 0.0f, 8.0f, 5.5f, navy);
        line(0.0f, 0.0f, 8.0f, 5.5f, white, 1.0f);
        line(8.0f, 0.0f, 0.0f, 5.5f, white, 1.0f);
        line(4.0f, 0.0f, 4.0f, 5.5f, white, 1.0f);
        line(0.0f, 2.75f, 8.0f, 2.75f, white, 1.0f);
        line(4.0f, 0.0f, 4.0f, 5.5f, red, 1.0f);
        line(0.0f, 2.75f, 8.0f, 2.75f, red, 1.0f);
    };

    const auto& c = currency;
    if (c == "USD") {
        bars_h({red, white, red, white, red, white, red});
        rect(0.0f, 0.0f, 7.2f, 6.05f, navy);
        disc(3.0f, 2.5f, 0.7f, white);
        disc(6.0f, 2.5f, 0.7f, white);
        disc(3.0f, 5.0f, 0.7f, white);
        disc(6.0f, 5.0f, 0.7f, white);
    } else if (c == "GBP") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, navy);
        line(0.0f, 0.0f, 16.0f, 11.0f, white, 1.5f);
        line(16.0f, 0.0f, 0.0f, 11.0f, white, 1.5f);
        line(8.0f, 0.0f, 8.0f, 11.0f, white, 3.0f);
        line(0.0f, 5.5f, 16.0f, 5.5f, white, 3.0f);
        line(8.0f, 0.0f, 8.0f, 11.0f, red, 1.5f);
        line(0.0f, 5.5f, 16.0f, 5.5f, red, 1.5f);
    } else if (c == "EUR") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, navy);
        for (int i = 0; i < 6; ++i) {
            const float angle = static_cast<float>(i) * 3.14159f / 3.0f;
            disc(8.0f + 3.5f * std::cos(angle),
                5.5f + 2.5f * std::sin(angle), 0.7f, yellow);
        }
    } else if (c == "JPY") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, white);
        disc(8.0f, 5.5f, 3.0f, red);
    } else if (c == "CHF") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, red);
        rect(6.5f, 2.0f, 9.5f, 9.0f, white);
        rect(4.0f, 4.5f, 12.0f, 7.5f, white);
    } else if (c == "AUD" || c == "NZD") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, navy);
        canton_union();
        disc(4.0f, 8.0f, 0.8f, white);
        disc(12.0f, 3.0f, 0.8f, white);
        disc(13.0f, 7.0f, 0.8f, white);
        disc(10.0f, 9.5f, 0.8f, white);
        if (c == "NZD")
            disc(12.0f, 5.0f, 0.8f, red);
    } else if (c == "CAD") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, white);
        rect(0.0f, 0.0f, 4.0f, 11.0f, red);
        rect(12.0f, 0.0f, 16.0f, 11.0f, red);
        tri(8.0f, 2.0f, 5.0f, 7.0f, 11.0f, 7.0f, red);
        rect(7.5f, 7.0f, 8.5f, 9.0f, red);
    } else if (c == "DKK") {
        nordic(red, white, false);
    } else if (c == "SEK") {
        nordic(blue, yellow, false);
    } else if (c == "NOK") {
        nordic(red, white, true);
    } else if (c == "IDR") {
        bars_h({red, white});
    } else if (c == "PLN") {
        bars_h({white, red});
    } else if (c == "HUF") {
        bars_h({red, white, green});
    } else if (c == "COP") {
        bars_h({yellow, blue, red});
    } else if (c == "INR") {
        bars_h({saffron, white, green});
        ring(8.0f, 5.5f, 2.0f, navy, 0.8f);
    } else if (c == "MXN") {
        bars_v({green, white, red});
        disc(8.0f, 5.5f, 1.0f, brown);
    } else if (c == "BRL") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, green);
        quad(8.0f, 1.5f, 13.5f, 5.5f, 8.0f, 9.5f, 2.5f, 5.5f, yellow);
        disc(8.0f, 5.5f, 2.0f, navy);
    } else if (c == "CZK") {
        bars_h({white, red});
        tri(0.0f, 0.0f, 6.0f, 5.5f, 0.0f, 11.0f, navy);
    } else if (c == "KRW") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, white);
        disc(8.0f, 5.5f, 2.6f, red);
        disc(8.0f, 5.5f, 1.3f, blue);
    } else if (c == "TRY") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, red);
        disc(7.0f, 5.5f, 2.6f, white);
        disc(8.3f, 5.5f, 2.1f, red);
        disc(10.5f, 5.5f, 0.8f, white);
    } else if (c == "ZAR") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, green);
        tri(0.0f, 0.0f, 6.0f, 5.5f, 0.0f, 11.0f, black);
        rect(6.0f, 1.0f, 16.0f, 3.5f, red);
        rect(6.0f, 7.5f, 16.0f, 10.0f, blue);
    } else if (c == "CNY") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, red);
        disc(3.5f, 3.0f, 1.1f, yellow);
        disc(7.0f, 2.0f, 0.6f, yellow);
        disc(8.0f, 5.0f, 0.6f, yellow);
    } else if (c == "HKD") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, red);
        disc(8.0f, 5.5f, 2.2f, white);
        disc(8.0f, 5.5f, 1.2f, red);
    } else if (c == "ILS") {
        rect(0.0f, 0.0f, 16.0f, 11.0f, white);
        rect(0.0f, 1.0f, 16.0f, 2.5f, navy);
        rect(0.0f, 8.5f, 16.0f, 10.0f, navy);
        disc(8.0f, 5.5f, 1.0f, navy);
    } else if (c == "CLP") {
        bars_h({white, red});
        rect(0.0f, 0.0f, 6.0f, 5.5f, navy);
        disc(3.0f, 2.8f, 0.8f, white);
    } else if (c == "SGD") {
        bars_h({red, white});
        disc(4.0f, 2.8f, 1.6f, white);
        disc(5.0f, 2.8f, 1.3f, red);
        disc(7.0f, 2.0f, 0.5f, white);
        disc(7.5f, 4.0f, 0.5f, white);
    } else {
        rect(0.0f, 0.0f, 16.0f, 11.0f, grey);
    }

    draw->AddRect(ImVec2(p.x, p.y),
        ImVec2(p.x + width, p.y + height), IM_COL32(0, 0, 0, 90));
}

}
