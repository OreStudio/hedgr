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
#include "hedgr.ui/widget/panels.hpp"

#include "hedgr.model/domain/probe.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <numeric>

namespace hedgr::ui::widget {

dashboard_counts count_instances(
    const hedgr::model::state::instance_table& instances,
    const hedgr::model::state::probe_table& probes) {
    using hedgr::model::domain::reachability;
    using hedgr::model::domain::readiness;

    dashboard_counts counts;
    const auto& layout = instances.layout();

    /*
     * The rows span includes the holes a removal leaves, so the walk checks the
     * slab first. Reading the probe for a live slot is a lookup by handle, which
     * is why a stale result can never be counted against the row that took the
     * slot after it.
     */
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        ++counts.total;

        const hedgr::model::state::instance_handle handle{
            index, layout.generation[index]};
        const auto* probe = probes.get(handle);
        if (probe == nullptr)
            continue;

        if (probe->reach == reachability::unreachable)
            ++counts.unreachable;
        if (probe->ready == readiness::ready)
            ++counts.ready;
        else if (probe->ready == readiness::bootstrapping)
            ++counts.bootstrapping;
    }

    return counts;
}

session_facts collect_session_facts(
    const hedgr::model::state::session_table& sessions,
    const hedgr::model::state::account_table& accounts,
    hedgr::model::state::session_handle handle) {
    session_facts facts;
    if (!sessions.contains(handle))
        return facts;

    facts.found = true;
    facts.instance_id = std::string{sessions.instance_id(handle)};
    facts.account_id = std::string{sessions.account_id(handle)};
    facts.tenant = std::string{sessions.tenant_name(handle)};
    facts.party = std::string{sessions.party_name(handle)};
    facts.opened_at_ns = sessions.opened_at_ns(handle);
    facts.latency_us = sessions.latency_us(handle);
    facts.status = sessions.status(handle);
    facts.last_error = std::string{sessions.last_error(handle)};

    /*
     * The account is resolved through its table rather than stored on the
     * session, because the name a person gave an account is display text and
     * the session holds the key. A session whose account row has gone still
     * reports, with the id where the name would have been: a workspace that
     * outlives its account must say which account it was.
     */
    if (const auto account = accounts.find(facts.account_id)) {
        const auto* row = accounts.get(*account);
        facts.principal = row->principal;
        facts.account_name = row->name.empty() ? row->principal : row->name;
    } else {
        facts.account_name = facts.account_id;
    }

    return facts;
}

sign_in_result last_sign_in(
    const hedgr::model::state::session_table& sessions,
    std::string_view instance_id, std::string_view account_id) {
    sign_in_result result;
    const auto handle = sessions.find(instance_id, account_id);
    if (!handle)
        return result;

    result.ever_attempted = true;
    result.status = sessions.status(*handle);
    result.tenant = std::string{sessions.tenant_name(*handle)};
    result.last_error = std::string{sessions.last_error(*handle)};
    return result;
}

host_port split_host_port(std::string_view url) {
    host_port parts;

    std::string_view rest = url;
    const auto scheme = rest.find("://");
    if (scheme != std::string_view::npos) {
        parts.scheme = std::string{rest.substr(0, scheme + 3)};
        rest.remove_prefix(scheme + 3);
    }

    /*
     * The last colon separates the port, not the first: an IPv6 address is
     * written in brackets with colons inside them, so only a colon after the
     * closing bracket can introduce one.
     */
    const auto brackets = rest.rfind(']');
    const auto colon = rest.rfind(':');
    if (colon != std::string_view::npos
        && (brackets == std::string_view::npos || colon > brackets)) {
        parts.host = std::string{rest.substr(0, colon)};
        parts.port = std::string{rest.substr(colon + 1)};
        return parts;
    }

    parts.host = std::string{rest};
    return parts;
}

std::string join_host_port(const host_port& parts) {
    std::string result = parts.scheme;
    result.append(parts.host);
    if (!parts.port.empty()) {
        result.push_back(':');
        result.append(parts.port);
    }
    return result;
}

std::vector<std::string> split_labels(std::string_view text) {
    std::vector<std::string> names;
    std::size_t start = 0;

    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        std::string_view piece = comma == std::string_view::npos
            ? text.substr(start)
            : text.substr(start, comma - start);

        const auto not_space = [](char c) {
            return std::isspace(static_cast<unsigned char>(c)) == 0;
        };
        const auto first = std::find_if(piece.begin(), piece.end(), not_space);
        const auto last = std::find_if(piece.rbegin(), piece.rend(), not_space).base();

        if (std::distance(first, last) > 0)
            names.emplace_back(first, last);

        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }

    return names;
}

std::string join_labels(const std::vector<std::string>& names) {
    return std::accumulate(names.begin(), names.end(), std::string{},
        [](std::string acc, const std::string& name) {
            if (!acc.empty())
                acc.append(", ");
            acc.append(name);
            return acc;
        });
}

}
