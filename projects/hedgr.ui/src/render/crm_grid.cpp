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
#include "hedgr.ui/render/crm_grid.hpp"
#include "ores.refdata.api/domain/currency_pair_convention.hpp"
#include "ores.refdata.client/presentation/currency_pair_rate_formatter.hpp"
#include <format>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace hedgr::ui::render {

namespace {

[[nodiscard]] const model::domain::crm_convention* find_convention(
    const std::vector<model::domain::crm_convention>& conventions,
    std::string_view base, std::string_view quote) {
    for (const auto& convention : conventions) {
        const std::string_view code = convention.pair_code;
        if (code.size() == base.size() + 1 + quote.size()
            && code.starts_with(base) && code[base.size()] == '/'
            && code.ends_with(quote))
            return &convention;
    }
    return nullptr;
}

/*
 * A market convention quotes one direction of a pair, so a cell in the other
 * direction finds its convention under the reversed code and tells the
 * formatter so.
 */
[[nodiscard]] std::string rate_text(const model::domain::crm_cell& cell,
    const std::vector<model::domain::crm_convention>& conventions) {
    using ores::refdata::client::presentation::currency_pair_rate_formatter;
    bool reversed = false;
    const auto* found = find_convention(conventions, cell.base_currency_code,
        cell.quote_currency_code);
    if (found == nullptr) {
        found = find_convention(conventions, cell.quote_currency_code,
            cell.base_currency_code);
        reversed = found != nullptr;
    }
    if (found == nullptr)
        return currency_pair_rate_formatter::format_rate(cell.rate,
            std::nullopt, false);

    ores::refdata::domain::currency_pair_convention convention;
    convention.pair_code = found->pair_code;
    convention.pip_factor = found->pip_factor;
    convention.tick_size = found->tick_size;
    convention.decimal_places = found->decimal_places;
    return currency_pair_rate_formatter::format_rate(cell.rate, convention,
        reversed);
}

}

crm_grid build_crm_grid(const model::state::crm_snapshot& batch,
    std::string_view crm_name, std::set<std::string>& seen) {
    std::map<std::pair<std::string_view, std::string_view>,
        const model::domain::crm_cell*> by_pair;
    for (const auto& cell : batch.cells) {
        if (cell.crm_name != crm_name)
            continue;
        seen.insert(cell.base_currency_code);
        seen.insert(cell.quote_currency_code);
        auto& slot = by_pair[{cell.base_currency_code,
            cell.quote_currency_code}];
        if (slot == nullptr
            || (slot->status == "unavailable" && cell.status != "unavailable"))
            slot = &cell;
    }

    crm_grid grid;
    grid.version = batch.version;
    grid.currencies.assign(seen.begin(), seen.end());
    grid.cells.resize(grid.currencies.size() * grid.currencies.size());
    for (const auto& [pair, cell] : by_pair) {
        if (cell->status == "fresh")
            ++grid.counts.fresh;
        else if (cell->status == "stale")
            ++grid.counts.stale;
        else if (cell->status == "disconnected")
            ++grid.counts.disconnected;
        else
            ++grid.counts.unavailable;
    }

    std::size_t at = 0;
    for (const auto& base : grid.currencies) {
        for (const auto& quote : grid.currencies) {
            auto& out = grid.cells[at++];
            if (base == quote)
                continue;
            out.pair = std::format("{}/{}", base, quote);
            const auto found = by_pair.find({base, quote});
            if (found == by_pair.end())
                continue;
            out.cell = found->second;
            if (out.cell->status == "unavailable")
                continue;
            out.rate = rate_text(*out.cell, batch.conventions);
            out.label = std::format("{}##{}", out.rate, out.pair);
        }
    }
    return grid;
}


const model::domain::market_series_row* crm_series_for_pair(
    const std::vector<model::domain::market_series_row>& series,
    const std::string_view party_id, const std::string_view base,
    const std::string_view quote,
    const std::span<const std::string> reference_providers) {
    const std::string path = std::format("/fx/{}?", base);
    const std::string field = std::format("ccy={}", quote);
    const model::domain::market_series_row* fallback = nullptr;
    for (const auto& row : series) {
        if (row.asset_class != "FX" || row.party_id != party_id)
            continue;
        if (row.uri.find(path) == std::string::npos
            || row.uri.find(field) == std::string::npos)
            continue;
        const bool reference = std::find(reference_providers.begin(),
            reference_providers.end(), row.provider_code)
            != reference_providers.end();
        if (!reference)
            return &row;
        if (fallback == nullptr)
            fallback = &row;
    }
    return fallback;
}

std::vector<crm_card> crm_pair_cards(
    const std::vector<model::domain::market_series_row>& series,
    const std::string_view party_id, const std::vector<std::string>& currencies,
    std::size_t& unmatched,
    const std::span<const std::string> reference_providers) {
    std::vector<crm_card> cards;
    std::set<std::string_view> taken;
    unmatched = 0;
    for (std::size_t i = 0; i < currencies.size(); ++i) {
        for (std::size_t j = i + 1; j < currencies.size(); ++j) {
            const auto* forward = crm_series_for_pair(series, party_id,
                currencies[i], currencies[j], reference_providers);
            const auto* reverse = crm_series_for_pair(series, party_id,
                currencies[j], currencies[i], reference_providers);
            if (forward == nullptr && reverse == nullptr) {
                ++unmatched;
                continue;
            }
            for (const auto* row : {forward, reverse}) {
                if (row != nullptr && taken.insert(row->id).second)
                    cards.push_back({row->id, row->label});
            }
        }
    }
    return cards;
}

}
