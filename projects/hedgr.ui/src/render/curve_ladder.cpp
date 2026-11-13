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
#include "hedgr.ui/render/curve_ladder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace hedgr::ui::render {

namespace {

namespace domain = hedgr::model::domain;
namespace state = hedgr::model::state;

/** @brief The newest point a held series has, and when it was observed. */
struct newest_tick final {
    double value = 0.0;
    std::int64_t at_ns = 0;
};

/**
 * @brief The newest point of @p series across its rings.
 *
 * The rings are read rather than the row's own newest stamp, because the ladder
 * shows a value and an age and the stamp alone cannot say which ring is the one
 * that has points. The fine ring is looked at first, because a ladder is a
 * live screen, and a row backfilled from history may only hold the coarser
 * rings.
 */
[[nodiscard]] std::optional<newest_tick> newest_of(
    const state::series_table& market, state::series_handle series) {
    std::optional<newest_tick> newest;
    for (const auto level : {domain::market_level::fine,
             domain::market_level::medium, domain::market_level::coarse}) {
        const auto columns = market.columns(series, level);
        if (columns.size == 0)
            continue;
        const auto at = columns.at_ns[columns.size - 1];
        if (newest && at <= newest->at_ns)
            continue;
        newest = newest_tick{.value = columns.value[columns.size - 1],
            .at_ns = at};
    }
    return newest;
}

/** @brief The series that quotes the pillar @p label, or empty. */
[[nodiscard]] std::string_view member_for(
    const domain::term_structure_state& terms, std::string_view label) {
    /*
     * The members are built in pillar order, so the search is over a handful of
     * short strings. Matching on the pillar's name and not on the position is
     * what keeps a curve whose axis holds a pillar its members do not (a
     * published curve carries no members at all) from putting one rung's value
     * on another rung's row.
     */
    for (std::size_t i = 0; i < terms.member_tenors.size(); ++i) {
        if (terms.member_tenors[i] != label)
            continue;
        if (i >= terms.member_ids.size())
            return {};
        return terms.member_ids[i];
    }
    return {};
}

}

void curve_ladder(const render_input& input,
    hedgr::model::state::session_handle session,
    const hedgr::model::domain::term_structure_state& terms,
    const hedgr::model::state::curve_row& row,
    const hedgr::model::state::curve_axis& axis,
    std::span<ladder_rung> out) {
    const auto pillars = axis.pillars.size();
    const auto rows = std::min(out.size(), pillars);
    const bool has_curve = !row.as_of_ns.empty()
        && row.values.size() == row.as_of_ns.size() * pillars;
    const auto newest_instant
        = row.as_of_ns.empty() ? 0 : row.as_of_ns.size() - 1;

    const auto instance_id = input.sessions.instance_id(session);
    const auto account_id = input.sessions.account_id(session);

    for (std::size_t i = 0; i < rows; ++i) {
        ladder_rung& rung = out[i];
        rung = ladder_rung{.label = axis.labels[i]};

        if (has_curve) {
            const double value = row.values[newest_instant * pillars + i];
            if (std::isfinite(value)) {
                rung.mid = value;
                rung.at_ns = row.as_of_ns[newest_instant];
            }
        }

        const auto member = member_for(terms, rung.label);
        if (member.empty())
            continue;
        const auto found
            = input.market.find(instance_id, account_id, member);
        if (!found || !input.market.contains(*found))
            continue;
        const auto tick = newest_of(input.market, *found);
        if (!tick || !std::isfinite(tick->value) || tick->at_ns <= rung.at_ns)
            continue;

        rung.mid = tick->value;
        rung.at_ns = tick->at_ns;
        rung.ticking = true;
    }
}

}
