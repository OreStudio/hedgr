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
#ifndef HEDGR_UI_WIDGET_FLAGS_HPP
#define HEDGR_UI_WIDGET_FLAGS_HPP

#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include <string>
#include <string_view>

namespace hedgr::ui::widget {

/** @brief The width of a flag at its natural height. */
inline constexpr float flag_width = 16.0f;

/** @brief The height of a flag, one row of text. */
inline constexpr float flag_height = 11.0f;

/**
 * @brief Whether this build knows a currency's flag.
 *
 * A currency the deployment adds after this build gets no flag rather than a
 * wrong one; the row still names it in full.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool has_flag(std::string_view currency);

/** @brief The currencies a series label names, base and quote. */
struct currency_pair final {
    std::string base;
    std::string quote;
};

/**
 * @brief Reads the currencies out of a label the market catalogue produced.
 *
 * An FX label is "FX GBP/USD" and an interest-rate label is "IR GBP 1D", so
 * the pair comes from the word after the asset class. A label with no currency
 * in it, or a currency this build does not know, yields empty codes and the
 * row is drawn without flags.
 */
[[nodiscard]] HEDGR_UI_EXPORT currency_pair
currencies_of(std::string_view label);

/**
 * @brief Draws one currency's flag at @p top_left, sixteen by eleven pixels by default.
 *
 * The flags are drawn rather than decoded, because a flag needs a few bars and
 * a canton and nothing more, and a sprite sheet would put a decoder, an upload
 * and an asset licence on the path that draws a list. The artwork is simplified
 * to what identifies the currency, and @p scale lets a larger surface draw the
 * same artwork bigger. The series list uses the natural size; the chart header
 * asks for twice it.
 */
HEDGR_UI_EXPORT void draw_flag(ImDrawList* draw, ImVec2 top_left,
    std::string_view currency, float scale = 1.0f, ImTextureID texture = 0);

}

#endif
