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
#include "hedgr.model/state/curve_table.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <utility>

namespace hedgr::model::state {

namespace {

/** @brief The running key one curve is interned by. */
[[nodiscard]] std::string curve_key(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    return std::format("{}\x1f{}\x1f{}", instance_id, account_id, series_id);
}

/**
 * @brief How many years a pillar names, for ordering.
 *
 * Tenors arrive as text ("1M", "10Y", "ON"). Parsing them once, when the curve
 * is stored, is what lets a plot draw them in order without touching a string
 * on the frame thread. A pillar that is not a tenor sorts last, by name.
 */
[[nodiscard]] double pillar_years(std::string_view pillar) {
    if (pillar.empty())
        return -1.0;
    auto at = std::size_t{0};
    double amount = 0.0;
    const auto first = pillar.data();
    const auto last = pillar.data() + pillar.size();
    const auto parsed = std::from_chars(first, last, amount);
    if (parsed.ec != std::errc{})
        return -1.0;
    at = static_cast<std::size_t>(parsed.ptr - first);
    double scale = 0.0;
    if (at < pillar.size()) {
        switch (static_cast<char>(
            std::toupper(static_cast<unsigned char>(pillar[at])))) {
        case 'D': scale = 1.0 / 365.0; break;
        case 'W': scale = 7.0 / 365.0; break;
        case 'M': scale = 1.0 / 12.0; break;
        case 'Y': scale = 1.0; break;
        default: return -1.0;
        }
    } else {
        return -1.0;
    }
    return amount * scale;
}

}

curve_handle curve_table::intern(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    const auto key = curve_key(instance_id, account_id, series_id);
    if (const auto found = by_key_.find(key); found != by_key_.end()) {
        const auto index = found->second;
        return curve_handle{index, generation_[index]};
    }

    const auto index = static_cast<std::uint32_t>(rows_.size());
    rows_.push_back(slot{.instance_id = std::string{instance_id},
        .account_id = std::string{account_id},
        .row = curve_row{.series_id = std::string{series_id}}});
    alive_.push_back(1);
    generation_.push_back(0);
    by_key_.emplace(key, index);
    return curve_handle{index, generation_[index]};
}

std::optional<curve_handle> curve_table::find(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) const {
    const auto found =
        by_key_.find(curve_key(instance_id, account_id, series_id));
    if (found == by_key_.end())
        return std::nullopt;
    return curve_handle{found->second, generation_[found->second]};
}

bool curve_table::contains(curve_handle h) const noexcept {
    return h.index < rows_.size() && generation_[h.index] == h.generation
        && alive_[h.index] != 0;
}

const curve_row* curve_table::get(curve_handle h) const noexcept {
    return contains(h) ? &rows_[h.index].row : nullptr;
}

curve_row* curve_table::get(curve_handle h) noexcept {
    return contains(h) ? &rows_[h.index].row : nullptr;
}

void curve_table::begin(curve_handle h) {
    auto* row = get(h);
    if (row == nullptr)
        return;
    row->state = curve_state::reading;
    row->version = ++version_;
}

void curve_table::apply(curve_handle h, std::string label,
    std::vector<std::string> pillars, std::vector<std::int64_t> as_of_ns,
    std::vector<double> values) {
    auto* row = get(h);
    if (row == nullptr)
        return;
    row->label = std::move(label);
    row->pillars = std::move(pillars);
    row->as_of_ns = std::move(as_of_ns);
    row->values = std::move(values);
    row->error.clear();
    row->state = curve_state::ready;
    row->version = ++version_;
}

void curve_table::fail(curve_handle h, std::string message) {
    auto* row = get(h);
    if (row == nullptr)
        return;
    row->error = std::move(message);
    row->state = curve_state::failed;
    row->version = ++version_;
}

void curve_table::clear() {
    rows_.clear();
    alive_.clear();
    generation_.clear();
    by_key_.clear();
}

std::uint64_t curve_table::version(curve_handle h) const noexcept {
    const auto* row = get(h);
    return row == nullptr ? 0 : row->version;
}

std::vector<std::size_t> curve_table::pillar_order(
    const std::vector<std::string>& pillars) {
    std::vector<std::size_t> order(pillars.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](auto left, auto right) {
        const auto a = pillar_years(pillars[left]);
        const auto b = pillar_years(pillars[right]);
        if (a == b)
            return pillars[left] < pillars[right];
        if (a < 0.0)
            return false;
        if (b < 0.0)
            return true;
        return a < b;
    });
    return order;
}

}
