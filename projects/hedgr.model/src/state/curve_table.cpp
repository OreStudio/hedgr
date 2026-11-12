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

/**
 * @brief The hash of one curve's three names, without touching the heap.
 *
 * A lookup runs once per curve per frame, so the names are hashed where they
 * already are rather than joined into a key string first. Two different names
 * may hash the same, which is why the slots a hash names are compared in full.
 */
[[nodiscard]] std::size_t curve_hash(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    constexpr std::size_t offset = 1469598103934665603ull;
    constexpr std::size_t prime = 1099511628211ull;
    auto hash = offset;
    const auto mix = [&hash](std::string_view text) {
        for (const char character : text) {
            hash ^= static_cast<unsigned char>(character);
            hash *= prime;
        }
        hash ^= 0x1f;
        hash *= prime;
    };
    mix(instance_id);
    mix(account_id);
    mix(series_id);
    return hash;
}

/**
 * @brief How many years an overnight convention names, or -1 for another name.
 *
 * The front of a curve is quoted by convention rather than by a number of
 * days: `ON` is today to tomorrow, `TN` tomorrow to the next day and `SN` the
 * day after. They are the shortest pillars there are, and a plot that sorted
 * them as "not a tenor" would put the overnight rate at the long end and draw
 * a curve that dives at its last point.
 */
[[nodiscard]] double overnight_years(std::string_view pillar) {
    if (pillar.size() < 2 || pillar.size() > 3)
        return -1.0;
    char folded[4]{};
    for (std::size_t i = 0; i < pillar.size(); ++i)
        folded[i] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(pillar[i])));
    const std::string_view name{folded, pillar.size()};
    if (name == "ON")
        return 0.5 / 365.0;
    if (name == "TN")
        return 1.0 / 365.0;
    if (name == "SN")
        return 2.0 / 365.0;
    return -1.0;
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
    if (const auto overnight = overnight_years(pillar); overnight >= 0.0)
        return overnight;
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
    if (const auto found = find(instance_id, account_id, series_id))
        return *found;

    const auto index = static_cast<std::uint32_t>(rows_.size());
    rows_.push_back(slot{.instance_id = std::string{instance_id},
        .account_id = std::string{account_id},
        .row = curve_row{.series_id = std::string{series_id}}});
    alive_.push_back(1);
    generation_.push_back(0);
    by_key_[curve_hash(instance_id, account_id, series_id)].push_back(index);
    return curve_handle{index, generation_[index]};
}

std::optional<curve_handle> curve_table::find(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) const {
    const auto found =
        by_key_.find(curve_hash(instance_id, account_id, series_id));
    if (found == by_key_.end())
        return std::nullopt;
    for (const auto index : found->second) {
        const auto& held = rows_[index];
        if (held.instance_id == instance_id
            && held.account_id == account_id
            && held.row.series_id == series_id)
            return curve_handle{index, generation_[index]};
    }
    return std::nullopt;
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

std::size_t curve_table::intern_axis(
    const std::vector<std::string>& pillars) {
    std::string key;
    for (const auto& pillar : pillars) {
        key += pillar;
        key.push_back('\x1f');
    }
    if (const auto found = by_pillars_.find(key); found != by_pillars_.end())
        return found->second;

    /*
     * The axis is heap-held and its labels point into its own names: a plot is
     * handed both and reads them every frame, so an axis that moved with the
     * vector's growth would leave the plot reading a freed string - a curve
     * drawn against garbage.
     */
    auto axis = std::make_unique<curve_axis>();
    axis->pillars = pillars;
    axis->positions.resize(pillars.size());
    axis->labels.resize(pillars.size());
    for (std::size_t i = 0; i < axis->positions.size(); ++i) {
        axis->positions[i] = static_cast<double>(i);
        axis->labels[i] = axis->pillars[i].c_str();
    }
    const auto index = axes_.size();
    axes_.push_back(std::move(axis));
    by_pillars_.emplace(std::move(key), index);
    return index;
}

void curve_table::apply(curve_handle h, std::string label,
    std::vector<std::string> pillars, std::vector<std::int64_t> as_of_ns,
    std::vector<double> values) {
    auto* row = get(h);
    if (row == nullptr)
        return;

    /*
     * The pillars are put in drawing order once, here, and the values are
     * permuted with them: a desk plots many curves, and a plot that sorted its
     * pillars per frame would pay that cost once per window rather than once
     * per read. The order is shared with every curve quoted on the same
     * pillars.
     */
    const auto order = pillar_order(pillars);
    std::vector<std::string> ordered;
    ordered.reserve(order.size());
    for (const auto at : order)
        ordered.push_back(pillars[at]);

    const auto instants = as_of_ns.size();
    const auto count = ordered.size();
    std::vector<double> permuted;
    if (values.size() == instants * count) {
        permuted.resize(values.size());
        for (std::size_t row_index = 0; row_index < instants; ++row_index) {
            for (std::size_t pillar = 0; pillar < count; ++pillar) {
                permuted[row_index * count + pillar]
                    = values[row_index * count + order[pillar]];
            }
        }
    }

    /*
     * Only the newest instants are kept: a curve read on a cadence for a day
     * would otherwise grow without bound, and a desk holds dozens of curves.
     */
    std::size_t drop = 0;
    if (instants > curve_history_limit)
        drop = instants - curve_history_limit;

    row->label = std::move(label);
    row->axis = intern_axis(ordered);
    row->as_of_ns.assign(as_of_ns.begin() + static_cast<std::ptrdiff_t>(drop),
        as_of_ns.end());
    if (permuted.size() == instants * count) {
        row->values.assign(permuted.begin() + static_cast<std::ptrdiff_t>(drop * count),
            permuted.end());
    } else {
        row->values.clear();
    }
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
    axes_.clear();
    by_pillars_.clear();
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
