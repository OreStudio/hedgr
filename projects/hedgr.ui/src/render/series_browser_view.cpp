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
#include "hedgr.ui/render/series_browser_view.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace hedgr::ui::render {

int tenor_rank(std::string_view tenor) {
    if (tenor.empty())
        return 0;
    int amount = 0;
    std::size_t i = 0;
    while (i < tenor.size()
        && std::isdigit(static_cast<unsigned char>(tenor[i]))) {
        amount = amount * 10 + (tenor[i] - '0');
        ++i;
    }
    if (amount == 0)
        amount = 1;
    if (i >= tenor.size())
        return amount;
    switch (static_cast<char>(std::toupper(
        static_cast<unsigned char>(tenor[i])))) {
    case 'D':
        return amount;
    case 'W':
        return amount * 7;
    case 'M':
        return amount * 30;
    case 'Y':
        return amount * 365;
    default:
        return amount;
    }
}

namespace {

namespace domain = hedgr::model::domain;

[[nodiscard]] bool contains_ci(std::string_view haystack,
    std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

/**
 * @brief Whether the text @p filter names something about @p instrument.
 *
 * The deployment's own facts and nothing read off them: the name it gave the
 * instrument, its currencies, its instrument and quote types, and the providers
 * behind it. A person types `aud` expecting both the AUD rates and every pair
 * quoted in AUD from either side, and the name of a pair already carries both
 * sides; the currency fields are consulted beside it so an instrument whose
 * name is a curve id is still found by its currency.
 */
[[nodiscard]] bool names_text(const domain::market_instrument& instrument,
    std::string_view filter) {
    if (contains_ci(instrument.name, filter)
        || contains_ci(instrument.currency, filter)
        || contains_ci(instrument.base_currency, filter)
        || contains_ci(instrument.quote_currency, filter)
        || contains_ci(instrument.instrument_type, filter)
        || contains_ci(instrument.quote_type, filter))
        return true;
    for (const auto& source : instrument.sources)
        if (contains_ci(source.provider_code, filter))
            return true;
    return false;
}

}

bool update_series_browser_view(series_browser_view& view,
    const series_browser_query& query,
    const std::vector<domain::market_instrument>& instruments) {
    const bool hidden_changed =
        view.hidden_providers.size() != query.hidden_providers.size()
        || !std::equal(view.hidden_providers.begin(),
            view.hidden_providers.end(), query.hidden_providers.begin());
    if (view.builds != 0 && view.generation == query.generation
        && view.filter == query.filter && view.kind == query.kind
        && view.provider == query.provider && !hidden_changed)
        return false;
    ++view.builds;
    view.generation = query.generation;
    view.filter = query.filter;
    view.kind = query.kind;
    view.provider = query.provider;
    view.hidden_providers.assign(query.hidden_providers.begin(),
        query.hidden_providers.end());

    const auto hidden = [&](std::string_view code) {
        return std::find(query.hidden_providers.begin(),
                   query.hidden_providers.end(), code)
            != query.hidden_providers.end();
    };
    /*
     * An instrument is listed while it still has a provider to show. Hiding a
     * reference provider must not hide a curve a data vendor also quotes, so
     * the test is whether anything visible is left, not whether anything is
     * hidden.
     */
    const auto sourced = [&](const domain::market_instrument& instrument) {
        if (instrument.sources.empty())
            return true;
        return std::any_of(instrument.sources.begin(),
            instrument.sources.end(), [&](const auto& source) {
                return !hidden(source.provider_code);
            });
    };
    const auto names_provider = [](const domain::market_instrument& instrument,
                                    std::string_view code) {
        return std::any_of(instrument.sources.begin(),
            instrument.sources.end(), [&](const auto& source) {
                return source.provider_code == code;
            });
    };

    view.rows.clear();
    view.kinds.clear();
    view.providers.clear();
    for (std::size_t i = 0; i < instruments.size(); ++i) {
        const auto& instrument = instruments[i];
        if (!query.kind.empty() && instrument.kind != query.kind)
            continue;
        if (!query.provider.empty()
            && !names_provider(instrument, query.provider))
            continue;
        if (!sourced(instrument))
            continue;
        if (!names_text(instrument, query.filter))
            continue;
        view.rows.push_back(static_cast<std::uint32_t>(i));
    }

    /*
     * The facets come from the rows, not the other way round: a kind with no
     * instrument is not offered, and every provider listed is one a row can be
     * narrowed to. The kinds keep the deployment's order, which is why they
     * need no ranking of their own; the providers are sorted so the combo reads
     * alphabetically whatever order the instruments arrived in.
     */
    view.kinds.clear();
    for (const auto at : view.rows) {
        const auto& kind = instruments[at].kind;
        if (std::find(view.kinds.begin(), view.kinds.end(), kind)
            == view.kinds.end())
            view.kinds.push_back(kind);
    }
    view.providers.clear();
    for (const auto at : view.rows)
        for (const auto& source : instruments[at].sources)
            if (std::find(view.providers.begin(), view.providers.end(),
                    source.provider_code)
                == view.providers.end())
                view.providers.push_back(source.provider_code);
    std::sort(view.providers.begin(), view.providers.end());
    return true;
}

bool update_party_list(party_list& list, std::string_view serialised,
    std::string_view session_party, std::string_view session_party_name) {
    if (list.builds != 0 && list.source == serialised
        && list.session_party == session_party
        && list.session_party_name == session_party_name)
        return false;
    ++list.builds;
    list.source = serialised;
    list.session_party = session_party;
    list.session_party_name = session_party_name;

    list.text.clear();
    list.entries.clear();
    while (!serialised.empty()) {
        const auto record_end = serialised.find('\x1e');
        const auto record = serialised.substr(0, record_end);
        const auto unit = record.find('\x1f');
        if (unit != std::string_view::npos) {
            const auto id_at = static_cast<std::uint32_t>(list.text.size());
            list.text.append(record.substr(0, unit));
            list.text.push_back('\0');
            const auto name_at = static_cast<std::uint32_t>(list.text.size());
            list.text.append(record.substr(unit + 1));
            list.text.push_back('\0');
            list.entries.push_back({id_at, name_at});
        }
        if (record_end == std::string_view::npos)
            break;
        serialised.remove_prefix(record_end + 1);
    }
    if (list.entries.empty() && !session_party.empty()) {
        const auto id_at = static_cast<std::uint32_t>(list.text.size());
        list.text.append(session_party);
        list.text.push_back('\0');
        const auto name_at = static_cast<std::uint32_t>(list.text.size());
        list.text.append(session_party_name);
        list.text.push_back('\0');
        list.entries.push_back({id_at, name_at});
    }

    /*
     * The option array a foot control borrows, built once with the parse.
     * text stops growing above, so the pointers it hands out stay valid until
     * the next parse.
     */
    list.names.clear();
    list.names.reserve(list.entries.size());
    for (std::size_t i = 0; i < list.entries.size(); ++i)
        list.names.push_back(list.name(i));
    return true;
}

namespace {

/**
 * @brief Whether two rows belong to one curve.
 *
 * Everything a rung must not vary: the instrument, its subject, its quoting,
 * its owner and its provider. The tenor is deliberately not here: on a swap
 * ladder every rung carries the same `1D` tenor and differs by `fwd_start`,
 * while on a ladder quoted by tenor the tenor *is* the rung. Whichever field
 * varies is found afterwards, and that is what the rungs are.
 */
[[nodiscard]] bool same_instrument(const domain::market_series_row& left,
    const domain::market_series_row& right) {
    return left.asset == right.asset
        && left.identity.scope == right.identity.scope
        && left.identity.instrument_type == right.identity.instrument_type
        && left.identity.quote_type == right.identity.quote_type
        && left.party_id == right.party_id
        && left.provider_code == right.provider_code;
}

/** @brief The text of one identity field of @p row, or empty. */
[[nodiscard]] std::string field_text(const domain::market_series_row& row,
    std::string_view name) {
    for (const auto& field : row.identity.fields)
        if (field.name == name)
            return field.text;
    return {};
}

}

curve_group curve_of(const std::vector<domain::market_series_row>& catalogue,
    const domain::market_series_row& clicked) {
    std::vector<const domain::market_series_row*> members;
    for (const auto& row : catalogue)
        if (same_instrument(row, clicked))
            members.push_back(&row);

    /*
     * The rung is the identity field whose text differs among the members. The
     * fields are in schema order and every member of one curve carries the same
     * set, so the first that differs is the axis - `fwd_start` on a swap ladder,
     * `tenor` where the ladder is quoted that way.
     */
    curve_group group;
    const auto* first = members.empty() ? &clicked : members.front();
    for (const auto& field : first->identity.fields) {
        for (const auto* member : members) {
            if (field_text(*member, field.name) != field.text) {
                group.field = field.name;
                break;
            }
        }
        if (!group.field.empty())
            break;
    }

    for (const auto* member : members) {
        group.members.push_back(member);
        group.rungs.push_back(group.field.empty()
                ? member->tenor
                : field_text(*member, group.field));
    }
    /* A plot reads a curve from its shortest rung, so the order is by rung. */
    std::vector<std::size_t> order(group.members.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&group](auto left, auto right) {
        const auto a = tenor_rank(group.rungs[left]);
        const auto b = tenor_rank(group.rungs[right]);
        if (a != b)
            return a < b;
        return group.rungs[left] < group.rungs[right];
    });
    curve_group ordered;
    ordered.field = std::move(group.field);
    for (const auto i : order) {
        ordered.members.push_back(group.members[i]);
        ordered.rungs.push_back(group.rungs[i]);
    }
    if (ordered.members.empty()) {
        ordered.members.push_back(&clicked);
        ordered.rungs.push_back(clicked.tenor);
    }
    return ordered;
}

}
