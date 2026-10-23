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
#include "hedgr.session/market/series_summary.hpp"

#include "hedgr.session/market/series_label.hpp"
#include <boost/uuid/uuid_io.hpp>

namespace hedgr::session::market {

transport::market_series_summary
summarise_series(const ores::marketdata::domain::market_series& series) {
    return transport::market_series_summary{
        .id = boost::uuids::to_string(series.id),
        .oresmd_uri = series.oresmd_uri,
        .label = series_label(series.oresmd_uri),
        .asset_class = series_asset_class(series.oresmd_uri),
        .provider_code = series.provider_code,
        .party_id = boost::uuids::to_string(series.party_id),
        .tenor = series_term(series.oresmd_uri),
        .curve_key = series_curve_key(series.oresmd_uri),
        .curve_label = series_curve_label(series.oresmd_uri)};
}

}
