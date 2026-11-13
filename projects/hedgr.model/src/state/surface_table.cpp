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
#include "hedgr.model/state/surface_table.hpp"

#include <format>
#include <utility>

namespace hedgr::model::state {

namespace {

/** @brief The hash of one surface's three names, without touching the heap. */
[[nodiscard]] std::size_t surface_hash(std::string_view instance_id,
    std::string_view account_id, std::string_view blueprint_id) {
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
    mix(blueprint_id);
    return hash;
}

}

surface_handle surface_table::intern(std::string_view instance_id,
    std::string_view account_id, std::string_view blueprint_id) {
    if (const auto found = find(instance_id, account_id, blueprint_id))
        return *found;

    const auto index = static_cast<std::uint32_t>(rows_.size());
    rows_.push_back(slot{.instance_id = std::string{instance_id},
        .account_id = std::string{account_id},
        .row = surface_row{.blueprint_id = std::string{blueprint_id}}});
    alive_.push_back(1);
    generation_.push_back(0);
    by_key_[surface_hash(instance_id, account_id, blueprint_id)].push_back(
        index);
    return surface_handle{index, generation_[index]};
}

std::optional<surface_handle> surface_table::find(std::string_view instance_id,
    std::string_view account_id, std::string_view blueprint_id) const {
    const auto found = by_key_.find(
        surface_hash(instance_id, account_id, blueprint_id));
    if (found == by_key_.end())
        return std::nullopt;
    for (const auto index : found->second) {
        const auto& held = rows_[index];
        if (held.instance_id == instance_id
            && held.account_id == account_id
            && held.row.blueprint_id == blueprint_id)
            return surface_handle{index, generation_[index]};
    }
    return std::nullopt;
}

bool surface_table::contains(surface_handle h) const noexcept {
    return h.index < rows_.size() && generation_[h.index] == h.generation
        && alive_[h.index] != 0;
}

const surface_row* surface_table::get(surface_handle h) const noexcept {
    return contains(h) ? &rows_[h.index].row : nullptr;
}

surface_row* surface_table::get(surface_handle h) noexcept {
    return contains(h) ? &rows_[h.index].row : nullptr;
}

void surface_table::begin(surface_handle h) {
    auto* row = get(h);
    if (row == nullptr)
        return;
    row->state = surface_state::reading;
    row->version = ++version_;
}

std::size_t surface_table::intern_axis(
    const std::vector<std::string>& names) {
    std::string key;
    for (const auto& name : names) {
        key += name;
        key.push_back('\x1f');
    }
    if (const auto found = by_pillars_.find(key); found != by_pillars_.end())
        return found->second;

    /*
     * Heap-held, with its labels pointing into its own names, for the reason
     * curve_table gives: an axis that moved with the vector's growth would
     * leave a plot reading a freed string.
     */
    auto axis = std::make_unique<curve_axis>();
    axis->pillars = names;
    axis->positions.resize(names.size());
    axis->labels.resize(names.size());
    for (std::size_t i = 0; i < axis->positions.size(); ++i) {
        axis->positions[i] = static_cast<double>(i);
        axis->labels[i] = axis->pillars[i].c_str();
    }
    const auto index = axes_.size();
    axes_.push_back(std::move(axis));
    by_pillars_.emplace(std::move(key), index);
    return index;
}

void surface_table::apply(surface_handle h, std::string label,
    std::string base_currency, std::string quote_currency,
    std::int64_t as_of_ns, std::vector<std::string> expiries,
    std::vector<std::string> strikes, std::vector<double> values) {
    auto* row = get(h);
    if (row == nullptr)
        return;

    /*
     * The expiries are put in tenor order once, here, and the values are
     * permuted with them, exactly as a curve's pillars are. The strikes keep
     * the deployment's own order: they name moneyness rather than a quantity a
     * reader would sort - a delta ladder runs put to call, and sorting its
     * names alphabetically would draw the smile inside out.
     */
    const auto order = curve_table::pillar_order(expiries);
    std::vector<std::string> ordered_expiries;
    ordered_expiries.reserve(order.size());
    for (const auto at : order)
        ordered_expiries.push_back(expiries[at]);

    const auto rows = ordered_expiries.size();
    const auto columns = strikes.size();
    std::vector<double> permuted;
    if (values.size() == rows * columns) {
        permuted.resize(values.size());
        for (std::size_t column = 0; column < columns; ++column) {
            for (std::size_t at = 0; at < rows; ++at) {
                permuted[at * columns + column]
                    = values[order[at] * columns + column];
            }
        }
    }

    row->label = std::move(label);
    row->base_currency = std::move(base_currency);
    row->quote_currency = std::move(quote_currency);
    row->as_of_ns = as_of_ns;
    row->expiry_axis = intern_axis(ordered_expiries);
    row->strike_axis = intern_axis(strikes);
    row->rows = rows;
    row->columns = columns;
    row->values = std::move(permuted);
    row->error.clear();
    row->state = surface_state::ready;
    row->version = ++version_;
}

void surface_table::fail(surface_handle h, std::string message) {
    auto* row = get(h);
    if (row == nullptr)
        return;
    row->error = std::move(message);
    row->state = surface_state::failed;
    row->version = ++version_;
}

void surface_table::clear() {
    rows_.clear();
    axes_.clear();
    by_pillars_.clear();
    alive_.clear();
    generation_.clear();
    by_key_.clear();
}

std::uint64_t surface_table::version(surface_handle h) const noexcept {
    const auto* row = get(h);
    return row == nullptr ? 0 : row->version;
}

}
