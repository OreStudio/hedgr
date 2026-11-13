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
#include "hedgr.session/market/series_label.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <map>

namespace hedgr::session::market {

namespace {

constexpr std::string_view scheme = "oresmd://";

struct parsed_uri final {
    std::string asset;
    std::string name;
    std::map<std::string, std::string> parameters;
};

[[nodiscard]] parsed_uri parse(std::string_view text) {
    parsed_uri result;
    if (text.substr(0, scheme.size()) != scheme)
        return result;

    auto rest = text.substr(scheme.size());
    const auto query = rest.find('?');
    const auto path = rest.substr(0, query);
    if (query != std::string_view::npos) {
        auto fields = rest.substr(query + 1);
        while (!fields.empty()) {
            const auto amp = fields.find('&');
            const auto field = fields.substr(0, amp);
            const auto eq = field.find('=');
            if (eq != std::string_view::npos) {
                result.parameters.emplace(std::string{field.substr(0, eq)},
                    std::string{field.substr(eq + 1)});
            }
            if (amp == std::string_view::npos)
                break;
            fields.remove_prefix(amp + 1);
        }
    }

    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
        result.name = std::string{path};
        return result;
    }
    result.asset = std::string{path.substr(0, slash)};
    result.name = std::string{path.substr(slash + 1)};
    return result;
}

[[nodiscard]] std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

[[nodiscard]] std::string parameter(const parsed_uri& uri,
    std::string_view key) {
    const auto found = uri.parameters.find(std::string{key});
    return found == uri.parameters.end() ? std::string{} : found->second;
}

}

std::string series_label(std::string_view oresmd_uri) {
    const auto uri = parse(oresmd_uri);
    if (uri.asset.empty() || uri.name.empty())
        return std::string{oresmd_uri};

    const auto asset = upper(uri.asset);
    if (uri.asset == "fx") {
        const auto quote = parameter(uri, "ccy");
        return quote.empty() ? std::format("{} {}", asset, uri.name)
                             : std::format("{} {}/{}", asset, uri.name, quote);
    }

    if (uri.asset == "ir") {
        const auto tenor = parameter(uri, "tenor");
        if (!tenor.empty())
            return std::format("{} {} {}", asset, uri.name, tenor);
        const auto curve = parameter(uri, "curve_id");
        if (!curve.empty())
            return std::format("{} {} {}", asset, uri.name, curve);
        return std::format("{} {}", asset, uri.name);
    }

    return std::format("{} {}", asset, uri.name);
}

std::string series_asset_class(std::string_view oresmd_uri) {
    return upper(parse(oresmd_uri).asset);
}

std::string series_term(std::string_view oresmd_uri) {
    const auto uri = parse(oresmd_uri);
    auto term = parameter(uri, "term");
    if (term.empty())
        term = parameter(uri, "tenor");
    return term;
}

std::string series_curve_key(std::string_view oresmd_uri) {
    const auto uri = parse(oresmd_uri);
    if (uri.asset.empty() || uri.name.empty())
        return {};
    if (parameter(uri, "tenor").empty() && parameter(uri, "term").empty())
        return {};

    std::string key = "oresmd://" + uri.asset + "/" + uri.name + "?";
    bool first = true;
    for (const auto& [name, value] : uri.parameters) {
        if (name == "tenor" || name == "term")
            continue;
        if (!first)
            key.push_back('&');
        key += name + "=" + value;
        first = false;
    }
    return key;
}

std::string series_curve_label(std::string_view oresmd_uri) {
    const auto uri = parse(oresmd_uri);
    if (uri.asset.empty() || uri.name.empty())
        return std::string{oresmd_uri};

    auto label = upper(uri.asset) + " " + uri.name;
    const auto instrument = parameter(uri, "instrument");
    if (!instrument.empty())
        label += " " + upper(instrument);
    const auto curve = parameter(uri, "curve_id");
    if (!curve.empty())
        label += " " + curve;

    /*
     * Every other field the curve's identity carries, so that two curves have
     * two names. A curve key is the URI without its term, and the fields left in
     * it are exactly what tells one curve of a currency from another: a swap
     * ladder is quoted per forward start, so `fwd_start=0D` and `fwd_start=3M`
     * are two curves - and a label that dropped the field named both of them
     * "IR AUD IR_SWAP", which is a list of rows nobody can tell apart.
     *
     * The values are appended bare, in the key's own order, because the fields
     * are few and their names are longer than the values a desk reads.
     */
    for (const auto& [name, value] : uri.parameters) {
        if (name == "type" || name == "tenor" || name == "term"
            || name == "instrument" || name == "curve_id" || name == "quote")
            continue;
        if (value.empty())
            continue;
        label += " " + value;
    }
    return label;
}

}
