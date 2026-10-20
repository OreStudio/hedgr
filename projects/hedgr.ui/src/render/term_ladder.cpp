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
#include "hedgr.ui/render/term_ladder.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

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

std::size_t term_ladder::position_of(std::string_view tenor) const {
    const auto after = std::upper_bound(by_tenor.begin(), by_tenor.end(), tenor,
        [this](std::string_view t, std::uint32_t at) {
            return t < std::string_view{rungs[at].tenor};
        });
    if (after == by_tenor.begin())
        return rungs.size();
    const auto at = *(after - 1);
    return rungs[at].tenor == tenor ? at : rungs.size();
}

bool update_term_ladder(term_ladder& ladder, const term_ladder_key& key,
    const std::vector<hedgr::model::domain::term_curve>& curves,
    std::size_t first, std::size_t primary) {
    if (ladder.key == key)
        return false;
    ladder.key = key;
    ++ladder.builds;

    const auto& nodes = curves[primary].nodes;
    ladder.rungs.clear();
    for (const auto& node : nodes)
        ladder.rungs.push_back({tenor_rank(node.tenor), node.tenor});
    std::sort(ladder.rungs.begin(), ladder.rungs.end(),
        [](const auto& a, const auto& b) { return a.rank < b.rank; });

    const auto size = ladder.rungs.size();
    ladder.xs.resize(size);
    ladder.labels.resize(size);
    ladder.by_tenor.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        ladder.xs[i] = static_cast<double>(i);
        ladder.labels[i] = ladder.rungs[i].tenor.empty()
            ? "spot" : ladder.rungs[i].tenor.c_str();
        ladder.by_tenor[i] = static_cast<std::uint32_t>(i);
    }
    std::sort(ladder.by_tenor.begin(), ladder.by_tenor.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            const auto order = ladder.rungs[a].tenor <=> ladder.rungs[b].tenor;
            return order != 0 ? order < 0 : a < b;
        });

    ladder.ys.resize(primary + 1 - first);
    for (std::size_t c = first; c <= primary; ++c) {
        auto& ys = ladder.ys[c - first];
        ys.assign(size, std::numeric_limits<double>::quiet_NaN());
        for (const auto& node : curves[c].nodes) {
            const auto at = ladder.position_of(node.tenor);
            if (at != size)
                ys[at] = node.value;
        }
    }
    return true;
}

}
