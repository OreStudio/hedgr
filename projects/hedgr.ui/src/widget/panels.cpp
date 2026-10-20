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
#include <array>
#include <cctype>
#include <cmath>
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
        else if (probe->ready == readiness::service_missing)
            ++counts.service_missing;
    }

    return counts;
}

estate_series read_estate_series(
    const hedgr::model::state::estate_history& history,
    double now_wall_seconds, std::int64_t now_mono_ns, double window_minutes) {
    using hedgr::model::state::estate_sample;

    estate_series series;

    const double now_minutes = now_wall_seconds / 60.0;
    const auto first_minute =
        static_cast<std::int64_t>(std::floor(now_minutes))
        - static_cast<std::int64_t>(window_minutes);
    const auto last_minute =
        static_cast<std::int64_t>(std::floor(now_minutes));
    const std::size_t point_count = last_minute >= first_minute
        ? static_cast<std::size_t>(last_minute - first_minute + 1)
        : 0;
    series.count = std::min(point_count, estate_series::capacity);

    /*
     * Fold the polls into minutes first, keeping the last poll of each minute.
     * A minute says where the estate stood at the end of it, and an average
     * would put a fraction of an instance on an axis measured in whole ones.
     */
    std::array<estate_sample, estate_series::capacity> buckets{};
    std::array<std::int64_t, estate_series::capacity> bucket_minutes{};
    std::size_t bucket_count = 0;
    std::int64_t current_minute = -1;
    const auto* samples = history.data();
    for (std::size_t index = 0; index < history.size(); ++index) {
        const auto& sample = samples[index];
        /*
         * The age is measured against the frame's monotonic clock rather than
         * against the newest sample, so a poll's wall time stays put as the run
         * advances and the points do not drift between polls.
         */
        const double age_s =
            static_cast<double>(now_mono_ns - sample.observed_at_ns) / 1e9;
        const auto minute = static_cast<std::int64_t>(
            std::floor((now_wall_seconds - age_s) / 60.0));
        if (minute < first_minute)
            continue;
        if (minute != current_minute) {
            if (bucket_count == buckets.size())
                break;
            bucket_minutes[bucket_count] = minute;
            current_minute = minute;
            ++bucket_count;
        }
        buckets[bucket_count - 1] = sample;
    }

    /*
     * Then walk the window's minutes and take the bucket belonging to each. A
     * minute with no bucket stays NaN in every series, which the plot draws as
     * a break rather than as a zero.
     */
    std::size_t bucket_index = 0;
    for (std::size_t index = 0; index < series.count; ++index) {
        const auto minute = first_minute + static_cast<std::int64_t>(index);
        series.minutes[index] = static_cast<double>(minute) + 0.5;

        const bool sampled = bucket_index < bucket_count
            && bucket_minutes[bucket_index] == minute;
        if (!sampled) {
            series.ready[index] = std::nan("");
            series.not_ready[index] = std::nan("");
            series.fault[index] = std::nan("");
            continue;
        }

        const auto& counts = buckets[bucket_index];
        /*
         * The sample carries five counts and the chart reads three. An instance
         * no probe has answered for is not ready and is not a fault, so it is
         * folded into the amber line rather than dropped, which keeps the three
         * lines summing to the estate. The two faults share the red line
         * because the operator asked for one red; they stay apart in the tree
         * and the details pane.
         */
        series.ready[index] = static_cast<double>(counts.ready);
        series.not_ready[index] =
            static_cast<double>(counts.not_ready + counts.unknown);
        series.fault[index] =
            static_cast<double>(counts.service_missing + counts.unreachable);
        ++bucket_index;
    }

    return series;
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
