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
#ifndef HEDGR_SESSION_MARKET_SERIES_SUMMARY_HPP
#define HEDGR_SESSION_MARKET_SERIES_SUMMARY_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "ores.marketdata.api/domain/market_series.hpp"

namespace hedgr::session::market {

/**
 * @brief The typed identity of @p uri, or nothing when it is not a series URI.
 *
 * Mirrors the deployment's own projection: the asset and quote spelling come
 * from the codec, the scope is the URI's subject, and every other identity
 * field follows in schema order. The party and provider are the series row's,
 * not the URI's.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT
std::optional<hedgr::model::domain::market_series_identity>
series_identity_from_uri(std::string_view uri, std::string_view party_id,
    std::string_view provider_code);

/**
 * @brief The catalogue row the interface lists for one series of the deployment.
 *
 * The id is the series' own uuid, which the deployment keys by item, provider
 * and party, so two providers that quote one item list as two rows with two
 * ids. The asset class, label and term are read from the URI, because the
 * deployment's asset classes of a series are a set and the URI is the identity.
 */
[[nodiscard]] HEDGR_SESSION_EXPORT transport::market_series_summary
summarise_series(const ores::marketdata::domain::market_series& series);

}

#endif
