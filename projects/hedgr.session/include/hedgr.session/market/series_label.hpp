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
#ifndef HEDGR_SESSION_MARKET_SERIES_LABEL_HPP
#define HEDGR_SESSION_MARKET_SERIES_LABEL_HPP

#include "hedgr.session/export.hpp"
#include <string>
#include <string_view>

namespace hedgr::session::market {

/**
 * @brief What a person calls a series, from its canonical oresmd URI.
 *
 * The deployment names a series by URI and nothing else, and the URI is the
 * only place its parts live. FX reads as a pair (GBP/USD), an interest-rate
 * series reads as a currency and a tenor or curve, and anything else falls back
 * to its asset class and instrument. The label is for a screen; the URI stays
 * the identity every request carries.
 *
 * A URI this function does not understand is returned whole rather than
 * guessed at, so a series a later deployment invents is still listed with a
 * name a person can match against the deployment's own record.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
series_label(std::string_view oresmd_uri);

/**
 * @brief The asset class a series belongs to, upper-cased, or empty.
 *
 * "fx", "ir" and "eq" in the URI are the same words the deployment uses; the
 * upper case is the interface's, so a menu heading reads as a heading.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
series_asset_class(std::string_view oresmd_uri);

/**
 * @brief The tenor or term a series varies over, or empty.
 *
 * A leaf rate series carries its tenor in the URI; a composite curve may carry
 * the term on each observation rather than in the series identity.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
series_term(std::string_view oresmd_uri);

/**
 * @brief The identity a term structure groups on, or empty.
 *
 * The series URI with its tenor and term removed, so the tenors of one curve
 * share a key. Empty when the URI names a point rather than a term of a curve.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
series_curve_key(std::string_view oresmd_uri);

/** @brief A curve's label: asset, subject, instrument and curve id, no term. */
[[nodiscard]] HEDGR_SESSION_EXPORT std::string
series_curve_label(std::string_view oresmd_uri);

}

#endif
