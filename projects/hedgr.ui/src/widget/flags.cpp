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

void fill(ImDrawList* draw, ImVec2 p, ImU32 colour) {
    draw->AddRectFilled(p, ImVec2(p.x + flag_width, p.y + flag_height),
        colour);
}

void bars_h(ImDrawList* draw, ImVec2 p, std::initializer_list<ImU32> colours) {
    const float band = flag_height / static_cast<float>(colours.size());
    float y = p.y;
    for (const auto colour : colours) {
        draw->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + flag_width, y + band),
            colour);
        y += band;
    }
}

void bars_v(ImDrawList* draw, ImVec2 p, std::initializer_list<ImU32> colours) {
    const float band = flag_width / static_cast<float>(colours.size());
    float x = p.x;
    for (const auto colour : colours) {
        draw->AddRectFilled(ImVec2(x, p.y), ImVec2(x + band, p.y + flag_height),
            colour);
        x += band;
    }
}

void nordic_cross(ImDrawList* draw, ImVec2 p, ImU32 field, ImU32 cross,
    bool inner) {
    fill(draw, p, field);
    const float vx = p.x + flag_width * 0.36f;
    draw->AddRectFilled(ImVec2(vx - 1.5f, p.y), ImVec2(vx + 1.5f, p.y + flag_height),
        cross);
    const float hy = p.y + flag_height * 0.5f;
    draw->AddRectFilled(ImVec2(p.x, hy - 1.5f), ImVec2(p.x + flag_width, hy + 1.5f),
        cross);
    if (inner) {
        draw->AddRectFilled(ImVec2(vx - 0.5f, p.y),
            ImVec2(vx + 0.5f, p.y + flag_height), blue);
        draw->AddRectFilled(ImVec2(p.x, hy - 0.5f),
            ImVec2(p.x + flag_width, hy + 0.5f), blue);
    }
}

void canton_union(ImDrawList* draw, ImVec2 p) {
    const float w = flag_width * 0.5f;
    const float h = flag_height * 0.5f;
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), navy);
    draw->AddLine(p, ImVec2(p.x + w, p.y + h), white, 1.0f);
    draw->AddLine(ImVec2(p.x + w, p.y), ImVec2(p.x, p.y + h), white, 1.0f);
    draw->AddLine(ImVec2(p.x + w * 0.5f, p.y),
        ImVec2(p.x + w * 0.5f, p.y + h), white, 1.0f);
    draw->AddLine(ImVec2(p.x, p.y + h * 0.5f),
        ImVec2(p.x + w, p.y + h * 0.5f), white, 1.0f);
    draw->AddLine(ImVec2(p.x + w * 0.5f, p.y),
        ImVec2(p.x + w * 0.5f, p.y + h), red, 1.0f);
    draw->AddLine(ImVec2(p.x, p.y + h * 0.5f),
        ImVec2(p.x + w, p.y + h * 0.5f), red, 1.0f);
}

}

bool has_flag(std::string_view currency) {
    static constexpr std::array<std::string_view, 27> known{
        "AUD", "BRL", "CAD", "CHF", "CLP", "CNY", "COP", "CZK", "DKK",
        "EUR", "GBP", "HKD", "HUF", "IDR", "ILS", "INR", "JPY", "KRW",
        "MXN", "NOK", "NZD", "PLN", "SEK", "SGD", "TRY", "USD", "ZAR"};
    for (const auto code : known) {
        if (code == currency)
            return true;
    }
    return false;
}

currency_pair currencies_of(std::string_view label) {
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
        return {has_flag(base) ? std::string{base} : std::string{},
            has_flag(quote) ? std::string{quote} : std::string{}};
    }
    return {has_flag(second) ? std::string{second} : std::string{}, {}};
}

void draw_flag(ImDrawList* draw, ImVec2 p, std::string_view currency) {
    const auto& c = currency;
    if (c == "USD") {
        bars_h(draw, p, {red, white, red, white, red, white, red});
        draw->AddRectFilled(p, ImVec2(p.x + flag_width * 0.45f,
                                 p.y + flag_height * 0.55f), navy);
        draw->AddCircleFilled(ImVec2(p.x + 3.0f, p.y + 2.5f), 0.7f, white);
        draw->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + 2.5f), 0.7f, white);
        draw->AddCircleFilled(ImVec2(p.x + 3.0f, p.y + 5.0f), 0.7f, white);
        draw->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + 5.0f), 0.7f, white);
    } else if (c == "GBP") {
        fill(draw, p, navy);
        draw->AddLine(p, ImVec2(p.x + flag_width, p.y + flag_height), white,
            1.5f);
        draw->AddLine(ImVec2(p.x + flag_width, p.y), ImVec2(p.x, p.y + flag_height),
            white, 1.5f);
        draw->AddLine(ImVec2(p.x + flag_width * 0.5f, p.y),
            ImVec2(p.x + flag_width * 0.5f, p.y + flag_height), white, 3.0f);
        draw->AddLine(ImVec2(p.x, p.y + flag_height * 0.5f),
            ImVec2(p.x + flag_width, p.y + flag_height * 0.5f), white, 3.0f);
        draw->AddLine(ImVec2(p.x + flag_width * 0.5f, p.y),
            ImVec2(p.x + flag_width * 0.5f, p.y + flag_height), red, 1.5f);
        draw->AddLine(ImVec2(p.x, p.y + flag_height * 0.5f),
            ImVec2(p.x + flag_width, p.y + flag_height * 0.5f), red, 1.5f);
    } else if (c == "EUR") {
        fill(draw, p, navy);
        for (int i = 0; i < 6; ++i) {
            const float angle = static_cast<float>(i) * 3.14159f / 3.0f;
            draw->AddCircleFilled(
                ImVec2(p.x + 8.0f + 3.5f * std::cos(angle),
                    p.y + 5.5f + 2.5f * std::sin(angle)),
                0.7f, yellow);
        }
    } else if (c == "JPY") {
        fill(draw, p, white);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 3.0f, red);
    } else if (c == "CHF") {
        fill(draw, p, red);
        draw->AddRectFilled(ImVec2(p.x + 6.5f, p.y + 2.0f),
            ImVec2(p.x + 9.5f, p.y + 9.0f), white);
        draw->AddRectFilled(ImVec2(p.x + 4.0f, p.y + 4.5f),
            ImVec2(p.x + 12.0f, p.y + 7.5f), white);
    } else if (c == "AUD" || c == "NZD") {
        fill(draw, p, navy);
        canton_union(draw, p);
        const ImU32 star = white;
        draw->AddCircleFilled(ImVec2(p.x + 4.0f, p.y + 8.0f), 0.8f, star);
        draw->AddCircleFilled(ImVec2(p.x + 12.0f, p.y + 3.0f), 0.8f, star);
        draw->AddCircleFilled(ImVec2(p.x + 13.0f, p.y + 7.0f), 0.8f, star);
        draw->AddCircleFilled(ImVec2(p.x + 10.0f, p.y + 9.5f), 0.8f, star);
        if (c == "NZD")
            draw->AddCircleFilled(ImVec2(p.x + 12.0f, p.y + 5.0f), 0.8f, red);
    } else if (c == "CAD") {
        fill(draw, p, white);
        draw->AddRectFilled(p, ImVec2(p.x + 4.0f, p.y + flag_height), red);
        draw->AddRectFilled(ImVec2(p.x + flag_width - 4.0f, p.y),
            ImVec2(p.x + flag_width, p.y + flag_height), red);
        draw->AddTriangleFilled(ImVec2(p.x + 8.0f, p.y + 2.0f),
            ImVec2(p.x + 5.0f, p.y + 7.0f), ImVec2(p.x + 11.0f, p.y + 7.0f),
            red);
        draw->AddRectFilled(ImVec2(p.x + 7.5f, p.y + 7.0f),
            ImVec2(p.x + 8.5f, p.y + 9.0f), red);
    } else if (c == "DKK") {
        nordic_cross(draw, p, red, white, false);
    } else if (c == "SEK") {
        nordic_cross(draw, p, blue, yellow, false);
    } else if (c == "NOK") {
        nordic_cross(draw, p, red, white, true);
    } else if (c == "IDR") {
        bars_h(draw, p, {red, white});
    } else if (c == "PLN") {
        bars_h(draw, p, {white, red});
    } else if (c == "HUF") {
        bars_h(draw, p, {red, white, green});
    } else if (c == "COP") {
        bars_h(draw, p, {yellow, blue, red});
    } else if (c == "INR") {
        bars_h(draw, p, {saffron, white, green});
        draw->AddCircle(ImVec2(p.x + 8.0f, p.y + 5.5f), 2.0f, navy, 8, 0.8f);
    } else if (c == "MXN") {
        bars_v(draw, p, {green, white, red});
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 1.0f, brown);
    } else if (c == "BRL") {
        fill(draw, p, green);
        draw->AddQuadFilled(ImVec2(p.x + 8.0f, p.y + 1.5f),
            ImVec2(p.x + 13.5f, p.y + 5.5f), ImVec2(p.x + 8.0f, p.y + 9.5f),
            ImVec2(p.x + 2.5f, p.y + 5.5f), yellow);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 2.0f, navy);
    } else if (c == "CZK") {
        bars_h(draw, p, {white, red});
        draw->AddTriangleFilled(p, ImVec2(p.x + 6.0f, p.y + 5.5f),
            ImVec2(p.x, p.y + flag_height), navy);
    } else if (c == "KRW") {
        fill(draw, p, white);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 2.6f, red);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 1.3f, blue);
    } else if (c == "TRY") {
        fill(draw, p, red);
        draw->AddCircleFilled(ImVec2(p.x + 7.0f, p.y + 5.5f), 2.6f, white);
        draw->AddCircleFilled(ImVec2(p.x + 8.3f, p.y + 5.5f), 2.1f, red);
        draw->AddCircleFilled(ImVec2(p.x + 10.5f, p.y + 5.5f), 0.8f, white);
    } else if (c == "ZAR") {
        fill(draw, p, green);
        draw->AddTriangleFilled(p, ImVec2(p.x + 6.0f, p.y + 5.5f),
            ImVec2(p.x, p.y + flag_height), black);
        draw->AddRectFilled(ImVec2(p.x + 6.0f, p.y + 1.0f),
            ImVec2(p.x + flag_width, p.y + 3.5f), red);
        draw->AddRectFilled(ImVec2(p.x + 6.0f, p.y + 7.5f),
            ImVec2(p.x + flag_width, p.y + 10.0f), blue);
    } else if (c == "CNY") {
        fill(draw, p, red);
        draw->AddCircleFilled(ImVec2(p.x + 3.5f, p.y + 3.0f), 1.1f, yellow);
        draw->AddCircleFilled(ImVec2(p.x + 7.0f, p.y + 2.0f), 0.6f, yellow);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.0f), 0.6f, yellow);
    } else if (c == "HKD") {
        fill(draw, p, red);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 2.2f, white);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 1.2f, red);
    } else if (c == "ILS") {
        fill(draw, p, white);
        draw->AddRectFilled(ImVec2(p.x, p.y + 1.0f),
            ImVec2(p.x + flag_width, p.y + 2.5f), navy);
        draw->AddRectFilled(ImVec2(p.x, p.y + flag_height - 2.5f),
            ImVec2(p.x + flag_width, p.y + flag_height - 1.0f), navy);
        draw->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 5.5f), 1.0f, navy);
    } else if (c == "CLP") {
        bars_h(draw, p, {white, red});
        draw->AddRectFilled(p, ImVec2(p.x + 6.0f, p.y + 5.5f), navy);
        draw->AddCircleFilled(ImVec2(p.x + 3.0f, p.y + 2.8f), 0.8f, white);
    } else if (c == "SGD") {
        bars_h(draw, p, {red, white});
        draw->AddCircleFilled(ImVec2(p.x + 4.0f, p.y + 2.8f), 1.6f, white);
        draw->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + 2.8f), 1.3f, red);
        draw->AddCircleFilled(ImVec2(p.x + 7.0f, p.y + 2.0f), 0.5f, white);
        draw->AddCircleFilled(ImVec2(p.x + 7.5f, p.y + 4.0f), 0.5f, white);
    } else {
        fill(draw, p, grey);
    }

    /* A hairline border, so a white flag is not a hole in the row. */
    draw->AddRect(p, ImVec2(p.x + flag_width, p.y + flag_height),
        IM_COL32(0, 0, 0, 90));
}

}
