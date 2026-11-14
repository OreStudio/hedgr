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

    /**
     * @brief The instrument kind to list, or empty for every kind.
     *
     * The deployment's own word - `pair`, `ladder`, `curve`, `surface`. It is a
     * facet, not a mode: narrowing to one kind never changes what a row is, and
     * `All` really means all. There is deliberately no asset-class facet: the
     * catalogue does not carry an asset class, so offering one would be this
     * client inventing a grouping the deployment did not state.
     */
    std::string_view kind;

    /** @brief The provider to list, or empty for every provider. */
    std::string_view provider;

    /** @brief Providers whose rows are not listed, usually the reference ones. */
    std::span<const std::string> hidden_providers;
};

/**
 * @brief The browser's rows, filtered and ordered, kept between frames.
 *
 * A catalogue changes when it is read, about once, and a frame draws sixty
 * times a second, so the filtering happens when the query changes. The rows are
 * positions in the instrument list and never pointers: the vector is replaced
 * as a whole by a refresh, and a position is checked against the generation
 * that made it.
 *
 * The rows are instruments, one per row, in the deployment's order: by kind and
 * then by name. That is why nothing here sorts - the answer already reads the
 * way the screen should, and a second order could only disagree with it.
 */
struct series_browser_view final {
    /** @brief Positions in the instruments, in the order the browser lists them. */
    std::vector<std::uint32_t> rows;

    /**
     * @brief The kinds the rows hold, each once, in the deployment's order.
     *
     * Taken from the instruments rather than from a table of this client's, so
     * a kind the deployment stops answering stops being offered.
     */
    std::vector<std::string> kinds;

    /**
     * @brief The providers the rows name, each once, sorted by code.
     *
     * Taken from the instruments for the kinds' reason.
     */
    std::vector<std::string> providers;

    /** @brief How many times the view has been rebuilt. */
    std::size_t builds = 0;

    /** @brief The query the view answers, valid once @c builds is above zero. */
    std::uint64_t generation = 0;
    std::string filter;
    std::string kind;
    std::string provider;
    std::vector<std::string> hidden_providers;
};

/**
 * @brief Rebuilds @p view when @p query differs from the one it answers.
 *
 * A row is an instrument, so a curve is one row rather than one per pillar or
 * per party, and a surface is a row rather than a second list. The text is
 * matched against the instrument's name, its currencies, its type and its
 * providers, which is what makes `aud` find both the AUD rates and the pairs
 * quoted in AUD from either side.
 *
 * A hidden provider hides an instrument only when no visible provider is left
 * behind it, so hiding reference definitions cannot make a curve vanish that a
 * vendor also quotes.
 *
 * @return whether it rebuilt.
 */
HEDGR_UI_EXPORT bool update_series_browser_view(series_browser_view& view,
    const series_browser_query& query,
    const std::vector<hedgr::model::domain::market_instrument>& instruments);

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
 * @brief One curve, as the series the catalogue holds for it.
 *
 * A curve is every series of one instrument that shares everything a rung must
 * not vary - the asset, the subject, the instrument and quote types, the tenor,
 * the party and the provider - and differs in one identity field, which is what
 * the rungs are. That field is not always `tenor`: an interest-rate swap ladder
 * is a series per `fwd_start` with the same `1D` tenor all the way up, so a
 * curve gathered by its URI-without-tenor answers with one rung.
 */
struct curve_group final {
    /** @brief The members, one per rung, in the order a plot draws them. */
    std::vector<const hedgr::model::domain::market_series_row*> members;

    /** @brief The rung each member is plotted under, index for index. */
    std::vector<std::string> rungs;

    /** @brief The identity field the rungs name, or empty when nothing varies. */
    std::string field;
};

/**
 * @brief Gathers the curve @p clicked belongs to, from the catalogue.
 *
 * Every series sharing the clicked row's asset, subject, instrument, quote,
 * tenor, party and provider is a member. The field whose text differs among
 * them names the rungs; when none differs the clicked row is the whole curve,
 * a one-point curve that is honestly one point.
 */
[[nodiscard]] HEDGR_UI_EXPORT curve_group curve_of(
    const std::vector<hedgr::model::domain::market_series_row>& catalogue,
    const hedgr::model::domain::market_series_row& clicked);

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
