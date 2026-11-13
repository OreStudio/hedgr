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
 * @brief Whether one of the currencies a series names contains @p filter.
 *
 * A currency is what a desk filters by: "AUD" is the IR curves quoted in AUD and
 * the FX pairs quoted in AUD on either side. The currencies come from the typed
 * identity - the subject and the quote field - so a pair is found from its quote
 * as well as from its base, and an interest-rate series is found at all. The
 * label is not consulted: it names the currency for a person, and the identity
 * is what the deployment stated.
 */
[[nodiscard]] bool names_currency(const domain::market_series_row& row,
    std::string_view filter) {
    if (filter.empty())
        return true;
    if (contains_ci(row.identity.scope, filter))
        return true;
    for (const auto& field : row.identity.fields) {
        if (field.name == "ccy" && contains_ci(field.text, filter))
            return true;
    }
    return false;
}

[[nodiscard]] bool is_ir(const domain::market_series_row& row) {
    if (row.asset != domain::market_asset::unknown)
        return row.asset == domain::market_asset::ir;
    return row.asset_class == "IR";
}

[[nodiscard]] bool listed_as_curve(const domain::market_series_row& row,
    const series_browser_query& query) {
    return is_ir(row)
        && (query.party.empty() || row.party_id == query.party)
        && (query.provider.empty() || row.provider_code == query.provider)
        && (contains_ci(row.label, query.filter)
            || contains_ci(row.uri, query.filter)
            || names_currency(row, query.filter));
}


[[nodiscard]] bool listed_as_series(const domain::market_series_row& row,
    const series_browser_query& query) {
    return (query.asset_class.empty() || row.asset_class == query.asset_class)
        && (query.party.empty() || row.party_id == query.party)
        && (query.provider.empty() || row.provider_code == query.provider)
        && (contains_ci(row.label, query.filter)
            || contains_ci(row.asset_class, query.filter)
            || contains_ci(row.uri, query.filter)
            || contains_ci(row.provider_code, query.filter)
            || names_currency(row, query.filter));
}

}

bool update_series_browser_view(series_browser_view& view,
    const series_browser_query& query,
    const std::vector<domain::market_series_row>& catalogue) {
    const bool hidden_changed =
        view.hidden_providers.size() != query.hidden_providers.size()
        || !std::equal(view.hidden_providers.begin(),
            view.hidden_providers.end(), query.hidden_providers.begin());
    if (view.builds != 0 && view.generation == query.generation
        && view.filter == query.filter
        && view.asset_class == query.asset_class
        && view.party == query.party
        && view.provider == query.provider && !hidden_changed)
        return false;
    ++view.builds;
    view.generation = query.generation;
    view.filter = query.filter;
    view.asset_class = query.asset_class;
    view.party = query.party;
    view.provider = query.provider;
    view.hidden_providers.assign(query.hidden_providers.begin(),
        query.hidden_providers.end());

    const auto hidden = [&](std::string_view code) {
        return std::find(query.hidden_providers.begin(),
                   query.hidden_providers.end(), code)
            != query.hidden_providers.end();
    };
    const auto wanted = domain::market_asset_of(query.asset_class);
    const bool curves = wanted != domain::market_asset::unknown
        ? wanted == domain::market_asset::ir
        : query.asset_class == "IR";
    view.rows.clear();
    std::unordered_set<std::string> listed;
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        if (hidden(catalogue[i].provider_code))
            continue;
        if (!curves) {
            if (listed_as_series(catalogue[i], query))
                view.rows.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        if (!listed_as_curve(catalogue[i], query))
            continue;
        /*
         * One row per curve, not per series: a curve published several times is
         * several series with one identity - five `AUD-AONIA` rows under one
         * name - and a person cannot tell five identical rows are one curve.
         * A row whose identity the catalogue could not parse has nothing to be
         * grouped by, so it stays a row of its own rather than collapsing into
         * its neighbours.
         */
        const auto& row = catalogue[i];
        if (row.identity.empty()) {
            view.rows.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        std::string key = row.asset_class;
        key += '\x1f';
        key += row.identity.scope;
        key += '\x1f';
        key += row.identity.instrument_type;
        key += '\x1f';
        key += row.identity.quote_type;
        key += '\x1f';
        key += row.party_id;
        key += '\x1f';
        key += row.provider_code;
        if (!listed.insert(std::move(key)).second)
            continue;
        view.rows.push_back(static_cast<std::uint32_t>(i));
    }
    std::stable_sort(view.rows.begin(), view.rows.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            const auto& left = catalogue[a];
            const auto& right = catalogue[b];
            const auto a_rank = tenor_rank(left.tenor);
            const auto b_rank = tenor_rank(right.tenor);
            if (a_rank != b_rank)
                return a_rank < b_rank;
            return curves ? curve_label_of(left) < curve_label_of(right)
                          : left.label < right.label;
        });
    return true;
}

bool update_asset_classes(series_browser_view& view, std::uint64_t generation,
    const std::vector<domain::market_series_row>& catalogue) {
    if (view.class_builds != 0 && view.classes_generation == generation)
        return false;
    ++view.class_builds;
    view.classes_generation = generation;
    view.classes.clear();
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        if (catalogue[i].asset_class.empty())
            continue;
        view.classes.push_back(static_cast<std::uint32_t>(i));
    }
    std::sort(view.classes.begin(), view.classes.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            return catalogue[a].asset_class < catalogue[b].asset_class;
        });
    view.classes.erase(std::unique(view.classes.begin(), view.classes.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            return catalogue[a].asset_class == catalogue[b].asset_class;
        }), view.classes.end());
    return true;
}

bool update_providers(series_browser_view& view, std::uint64_t generation,
    const std::vector<domain::market_series_row>& catalogue) {
    if (view.provider_builds != 0 && view.providers_generation == generation)
        return false;
    ++view.provider_builds;
    view.providers_generation = generation;
    view.providers.clear();
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        if (catalogue[i].provider_code.empty())
            continue;
        view.providers.push_back(static_cast<std::uint32_t>(i));
    }
    std::sort(view.providers.begin(), view.providers.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            return catalogue[a].provider_code < catalogue[b].provider_code;
        });
    view.providers.erase(std::unique(view.providers.begin(),
        view.providers.end(), [&](std::uint32_t a, std::uint32_t b) {
            return catalogue[a].provider_code == catalogue[b].provider_code;
        }), view.providers.end());
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
