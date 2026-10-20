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
 * Only the built-in artwork; a currency the deployment adds after this build
 * may still have a server image.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool has_flag(std::string_view currency);

/**
 * @brief Whether a currency can be drawn at all.
 *
 * The server's image wins when it sent one; the built-in artwork covers a
 * currency it did not. A currency with neither is left undrawn rather than
 * shown as a blank grey box.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool
flag_available(std::string_view currency, ImTextureID texture);

/** @brief The currencies a series label names, base and quote. */
struct currency_pair final {
    std::string base;
    std::string quote;
};

/**
 * @brief Reads the currencies out of a label the market catalogue produced.
 *
 * An FX label is "FX GBP/USD" and an interest-rate label is "IR GBP 1D", so
 * the pair comes from the word after the asset class. A word that is not an
 * ISO-shaped code (three capital letters) yields an empty code. Whether the
 * code has a flag is for flag_available, since the server may serve an image
 * for a currency this build has no artwork for.
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
