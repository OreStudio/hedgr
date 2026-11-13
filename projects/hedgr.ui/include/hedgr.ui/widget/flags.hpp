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

#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/export.hpp"
#include "imgui.h"
#include <string>
#include <string_view>

namespace hedgr::ui::widget {

/** @brief A flag's width over its height, the 16 by 11 of the server's images. */
inline constexpr float flag_aspect = 16.0f / 11.0f;

/** @brief The width of a flag @p height tall. */
[[nodiscard]] HEDGR_UI_EXPORT float flag_width_for(float height) noexcept;

/**
 * @brief The width of the flags that have a texture, zero when neither does.
 *
 * A currency with no loaded server image takes no space, and one flag takes
 * no gap.
 */
[[nodiscard]] HEDGR_UI_EXPORT float flag_pair_width(
    float height, ImTextureID base, ImTextureID quote) noexcept;

/** @brief The currencies a series names, base and quote. */
struct currency_pair final {
    std::string base;
    std::string quote;
};

/**
 * @brief Reads the currencies out of the identity the deployment gave a series.
 *
 * This is the authoritative reading and the one every screen uses. The identity
 * says the subject in the deployment's own words - the subject field of the
 * series' instrument type, which for a rate is the currency and for a spot is
 * the base - so `IR AUD DISCOUNT AUD-AONIA` is AUD from its scope alone and not
 * from the shape of its name. A spot's quote is its identity's `ccy` field.
 *
 * A subject that is not an ISO-shaped code yields an empty base: an equity
 * named AAPL has no flag, and guessing one from the label would be inventing
 * what the deployment did not say.
 */
[[nodiscard]] HEDGR_UI_EXPORT currency_pair
currencies_of(const hedgr::model::domain::market_series_identity& identity);

/**
 * @brief Reads the currencies out of a label the market catalogue produced.
 *
 * Only for a series whose identity was not parsed - a row restored from a
 * store older than the parse, or a published curve that is not a catalogue row.
 * An FX label is "FX GBP/USD" and an interest-rate label is "IR GBP 1D", so the
 * pair comes from the word after the asset class. A word that is not an
 * ISO-shaped code (three capital letters) yields an empty code. Whether the
 * server has an image for it is for the caller to look up.
 */
[[nodiscard]] HEDGR_UI_EXPORT currency_pair
currencies_of(std::string_view label);

/**
 * @brief Draws the server's flag images for a base and a quote, side by side.
 *
 * Every flag comes from the server, so a texture of zero means that image is
 * not loaded and nothing is drawn or reserved for it. The images are rounded
 * like a value pill, tinted a little translucent and edged in the card border,
 * and sit a couple of pixels apart so the two read as one badge.
 *
 * @return The width drawn, as flag_pair_width reports it.
 */
HEDGR_UI_EXPORT float draw_flag_pair(ImDrawList* draw, ImVec2 top_left,
    float height, ImTextureID base, ImTextureID quote);

}

#endif
