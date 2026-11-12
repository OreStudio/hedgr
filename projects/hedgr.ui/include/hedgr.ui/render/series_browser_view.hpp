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
#ifndef HEDGR_UI_RENDER_SERIES_BROWSER_VIEW_HPP
#define HEDGR_UI_RENDER_SERIES_BROWSER_VIEW_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::render {

/** @brief Everything the browser's visible rows are derived from. */
struct series_browser_query final {
    /** @brief The catalogue's generation, or 0 when it has none. */
    std::uint64_t generation = 0;
    std::string_view filter;
    std::string_view asset_class;
    std::string_view party;
    /** @brief The provider to list, or empty for every provider. */
    std::string_view provider;

    /** @brief Providers whose rows are not listed, usually the reference ones. */
    std::span<const std::string> hidden_providers;
};

/**
 * @brief The browser's rows, filtered and ordered, kept between frames.
 *
 * A catalogue changes when it is read, about once, and a frame draws sixty
 * times a second, so the filtering and the sort happen when the query changes.
 * The rows are positions in the catalogue's vector and never pointers: the
 * vector is replaced as a whole by a refresh, and a position is checked against
 * the generation that made it.
 */
struct series_browser_view final {
    /** @brief Positions in the catalogue, in the order the browser lists them. */
    std::vector<std::uint32_t> rows;

    /**
     * @brief For each asset class of the catalogue, the position of one row.
     *
     * Ordered by class name, so the combo lists a class once.
     */
    std::vector<std::uint32_t> classes;

    /**
     * @brief For each provider of the catalogue, the position of one row.
     *
     * Ordered by provider code, so the combo lists a provider once.
     */
    std::vector<std::uint32_t> providers;

    /** @brief How many times @c rows has been rebuilt. */
    std::size_t builds = 0;

    /** @brief How many times @c classes has been rebuilt. */
    std::size_t class_builds = 0;

    /** @brief How many times @c providers has been rebuilt. */
    std::size_t provider_builds = 0;

    /** @brief The query @c rows answers, valid once @c builds is above zero. */
    std::uint64_t generation = 0;
    std::string filter;
    std::string asset_class;
    std::string party;
    std::string provider;
    std::vector<std::string> hidden_providers;

    /** @brief The generation @c classes was taken at, valid once @c class_builds is above zero. */
    std::uint64_t classes_generation = 0;

    /** @brief The generation @c providers was taken at, valid once @c provider_builds is above zero. */
    std::uint64_t providers_generation = 0;
};

/**
 * @brief Rebuilds @p view when @p query differs from the one it answers.
 *
 * With the IR asset class the rows are the curves, one per IR series, matched
 * on label and URI and ordered by the tenor and the curve's label. Otherwise
 * they are the series, matched on label, asset class, URI and source and
 * ordered by tenor and label.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_series_browser_view(series_browser_view& view,
    const series_browser_query& query,
    const std::vector<hedgr::model::domain::market_series_row>& catalogue);

/**
 * @brief Rebuilds the asset class list when the catalogue's generation moved.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_asset_classes(series_browser_view& view,
    std::uint64_t generation,
    const std::vector<hedgr::model::domain::market_series_row>& catalogue);

/**
 * @brief Rebuilds the provider list when the catalogue's generation moved.
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_providers(series_browser_view& view,
    std::uint64_t generation,
    const std::vector<hedgr::model::domain::market_series_row>& catalogue);

/** @brief The name a curve is listed under. */
[[nodiscard]] inline const std::string& curve_label_of(
    const hedgr::model::domain::market_series_row& row) {
    return row.curve_label.empty() ? row.label : row.curve_label;
}

/**
 * @brief How many days a tenor names, for ordering.
 *
 * A tenor arrives as text ("1M", "10Y", "ON"). The browser lists the tenors of
 * a curve in the order a person reads them, so the text is parsed into an
 * amount of days once, when the list is built. A label that is not a tenor
 * sorts last.
 */
[[nodiscard]] HEDGR_UI_EXPORT int tenor_rank(std::string_view tenor);

/**
 * @brief The parties a login may act as, parsed from the session's column.
 *
 * The runtime serialises the list as id, unit separator, name, with a record
 * separator between parties. The parse is kept until that string, or the
 * session's own party, changes. Ids and names sit end to end in one buffer,
 * each terminated, so both read as C strings.
 */
struct party_list final {
    /** @brief The inputs the parse answers. */
    std::string source;
    std::string session_party;
    std::string session_party_name;

    /** @brief How many times it has been parsed. */
    std::size_t builds = 0;

    [[nodiscard]] std::size_t size() const { return entries.size(); }
    [[nodiscard]] bool empty() const { return entries.empty(); }
    [[nodiscard]] const char* id(std::size_t i) const {
        return text.data() + entries[i].id_at;
    }
    [[nodiscard]] const char* name(std::size_t i) const {
        return text.data() + entries[i].name_at;
    }

    /** @brief The index of the party with this id, or size() when absent. */
    [[nodiscard]] std::size_t index_of(std::string_view party_id) const {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (party_id == id(i))
                return i;
        return entries.size();
    }

    struct entry final {
        std::uint32_t id_at = 0;
        std::uint32_t name_at = 0;
    };
    std::string text;
    std::vector<entry> entries;

    /**
     * @brief The names as the option array a foot control borrows.
     *
     * Rebuilt with the parse and not per frame. Each points into @c text, so
     * the list must not be re-parsed while a frame is still reading it.
     */
    std::vector<const char*> names;
};

/**
 * @brief Parses @p serialised when it, or the session's party, changed.
 *
 * A login whose answer carried no list still has the party it is scoped to, so
 * the list is never empty while @p session_party is not.
 * @return whether it parsed.
 */
HEDGR_UI_EXPORT bool update_party_list(party_list& list,
    std::string_view serialised, std::string_view session_party,
    std::string_view session_party_name);

}

#endif
