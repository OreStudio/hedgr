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

#include "hedgr.ui/render/term_ladder.hpp"
#include <algorithm>
#include <cctype>

namespace hedgr::ui::render {

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

[[nodiscard]] bool listed_as_curve(const domain::market_series_row& row,
    const series_browser_query& query) {
    return row.asset_class == "IR"
        && (query.party.empty() || row.party_id == query.party)
        && (contains_ci(row.label, query.filter)
            || contains_ci(row.uri, query.filter));
}

[[nodiscard]] bool listed_as_series(const domain::market_series_row& row,
    const series_browser_query& query) {
    return (query.asset_class.empty() || row.asset_class == query.asset_class)
        && (query.party.empty() || row.party_id == query.party)
        && (contains_ci(row.label, query.filter)
            || contains_ci(row.asset_class, query.filter)
            || contains_ci(row.uri, query.filter)
            || contains_ci(row.provider_code, query.filter));
}

}

bool update_series_browser_view(series_browser_view& view,
    const series_browser_query& query,
    const std::vector<domain::market_series_row>& catalogue) {
    if (view.builds != 0 && view.generation == query.generation
        && view.filter == query.filter
        && view.asset_class == query.asset_class
        && view.party == query.party)
        return false;
    ++view.builds;
    view.generation = query.generation;
    view.filter = query.filter;
    view.asset_class = query.asset_class;
    view.party = query.party;

    const bool curves = query.asset_class == "IR";
    view.rows.clear();
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        if (curves ? listed_as_curve(catalogue[i], query)
                   : listed_as_series(catalogue[i], query))
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
    return true;
}

}
