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
#include "ores.marketdata.api/datum/market_datum.hpp"
#include "ores.marketdata.api/datum/schema.hpp"
#include "ores.marketdata.api/datum/value.hpp"
#include "ores.marketdata.core/datum/oresmd_uri_codec.hpp"
#include <boost/uuid/uuid_io.hpp>
#include <variant>

namespace hedgr::session::market {

std::optional<hedgr::model::domain::market_series_identity>
series_identity_from_uri(std::string_view uri, std::string_view party_id,
    std::string_view provider_code) {
    using namespace ores::marketdata::datum;

    const auto parsed = oresmd_uri_codec::read(uri);
    if (!parsed || !parsed->is_series())
        return std::nullopt;

    const auto& row = schema_of(parsed->type());
    hedgr::model::domain::market_series_identity identity;
    identity.asset = std::string(name_of(row.asset));
    identity.instrument_type
        = oresmd_uri_codec::instrument_spelling(parsed->type());
    identity.quote_type = oresmd_uri_codec::quote_spelling(parsed->quote());
    identity.party_id = std::string(party_id);
    identity.provider_code = std::string(provider_code);

    for (const auto& held : parsed->fields()) {
        if (std::holds_alternative<none_t>(held.held))
            continue;
        if (held.name == row.subject) {
            identity.scope = text_of(held.held);
        } else {
            identity.fields.push_back(
                {.name = std::string(name_of(held.name)),
                    .text = text_of(held.held)});
        }
    }
    return identity;
}

transport::market_series_summary
summarise_series(const ores::marketdata::domain::market_series& series) {
    const auto party = boost::uuids::to_string(series.party_id);
    auto identity = series_identity_from_uri(series.oresmd_uri, party,
        series.provider_code).value_or(
        hedgr::model::domain::market_series_identity{});
    const auto asset = hedgr::model::domain::market_asset_of(identity.asset);
    return transport::market_series_summary{
        .id = boost::uuids::to_string(series.id),
        .oresmd_uri = series.oresmd_uri,
        .label = series_label(series.oresmd_uri),
        .asset_class = series_asset_class(series.oresmd_uri),
        .provider_code = series.provider_code,
        .party_id = party,
        .tenor = series_term(series.oresmd_uri),
        .curve_key = series_curve_key(series.oresmd_uri),
        .curve_label = series_curve_label(series.oresmd_uri),
        .identity = std::move(identity),
        .asset = asset};
}

}
