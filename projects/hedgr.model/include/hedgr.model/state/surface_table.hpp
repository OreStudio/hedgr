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
#ifndef HEDGR_MODEL_STATE_SURFACE_TABLE_HPP
#define HEDGR_MODEL_STATE_SURFACE_TABLE_HPP

#include "hedgr.model/export.hpp"
#include "hedgr.model/state/curve_table.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/** @brief Where one published surface stands. */
enum class surface_state : std::uint8_t {
    /** @brief Nobody has asked for it. */
    unread = 0,
    /** @brief A read is out. */
    reading = 1,
    /** @brief The surface is held. */
    ready = 2,
    /** @brief The read failed; the error says why. */
    failed = 3,
};

/**
 * @brief One published FX volatility surface: two category axes and a grid.
 *
 * The same shape a curve has, with a second axis instead of a second instant:
 * the expiries name the rows, the strikes name the columns, and the values are
 * one flat block with a row per expiry. Both axes are the curve's own
 * @c curve_axis, because an axis is an ordered set of names and positions
 * whether it runs along the bottom of a plot or into it.
 *
 * A strike is not a number a reader would scale to: a deployment names its
 * columns by moneyness, `DEL/Spot/Put/25` and `ATM` among them. So the columns
 * are categories, and the height of the surface is the volatility.
 */
struct surface_row final {
    /** @brief The surface's name, as the deployment spells it. */
    std::string label;

    /** @brief The blueprint the surface belongs to. */
    std::string blueprint_id;

    /** @brief The pair's base and quote, as the blueprint stated them. */
    std::string base_currency;
    std::string quote_currency;

    /** @brief Why the last read failed, or empty. */
    std::string error;

    /** @brief Which shared axis the expiries use. */
    std::size_t expiry_axis = 0;

    /** @brief Which shared axis the strikes use. */
    std::size_t strike_axis = 0;

    /** @brief How many rows and columns the grid has. */
    std::size_t rows = 0;
    std::size_t columns = 0;

    /** @brief When the surface was published, epoch nanoseconds. */
    std::int64_t as_of_ns = 0;

    /** @brief @c rows * @c columns volatilities, row-major. */
    std::vector<double> values;

    surface_state state = surface_state::unread;

    /** @brief Bumped by every write, so a plot rebuilds only when it moved. */
    std::uint64_t version = 0;

    /** @brief Whether the grid holds as many values as its shape says. */
    [[nodiscard]] bool complete() const noexcept {
        return rows > 0 && columns > 0 && values.size() == rows * columns;
    }

    /** @brief The volatility at one cell, or zero outside the grid. */
    [[nodiscard]] double at(std::size_t row, std::size_t column) const noexcept {
        return complete() ? values[row * columns + column] : 0.0;
    }
};

/** @brief A surface's slot in the table. */
struct surface_handle final {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool operator==(const surface_handle&) const = default;
};

/**
 * @brief Every published surface a session holds, keyed by instance, account
 * and blueprint.
 *
 * The twin of @c curve_table, and deliberately the same shape: interning is
 * what makes two windows of one surface cost one read, and the axes are shared
 * between every surface quoted on them.
 */
class HEDGR_MODEL_EXPORT surface_table final {
public:
    /** @brief The row's slot, making one when the pair and blueprint is new. */
    [[nodiscard]] surface_handle intern(std::string_view instance_id,
        std::string_view account_id, std::string_view blueprint_id);

    /** @brief The slot for a surface already interned, or nothing. */
    [[nodiscard]] std::optional<surface_handle> find(std::string_view instance_id,
        std::string_view account_id, std::string_view blueprint_id) const;

    [[nodiscard]] bool contains(surface_handle h) const noexcept;
    [[nodiscard]] const surface_row* get(surface_handle h) const noexcept;
    [[nodiscard]] surface_row* get(surface_handle h) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    /** @brief Marks the surface as being read. */
    void begin(surface_handle h);

    /**
     * @brief Stores one read's answer.
     *
     * @param expiries the row names, in drawing order.
     * @param strikes the column names, in drawing order.
     * @param values row-major, @c expiries.size() * @c strikes.size() long.
     */
    void apply(surface_handle h, std::string label, std::string base_currency,
        std::string quote_currency, std::int64_t as_of_ns,
        std::vector<std::string> expiries, std::vector<std::string> strikes,
        std::vector<double> values);

    /** @brief Records a failed read and keeps whatever was held before. */
    void fail(surface_handle h, std::string message);

    /** @brief Forgets every surface. */
    void clear();

    /** @brief The version stamp of a row, zero when the handle is stale. */
    [[nodiscard]] std::uint64_t version(surface_handle h) const noexcept;

    /** @brief A shared axis by index. */
    [[nodiscard]] const curve_axis& axis(std::size_t index) const noexcept {
        return *axes_[index];
    }

    /** @brief How many distinct axes the held surfaces use. */
    [[nodiscard]] std::size_t axes() const noexcept { return axes_.size(); }

private:
    /** @brief The shared axis for @p names, making one when it is new. */
    [[nodiscard]] std::size_t intern_axis(const std::vector<std::string>& names);

    /** @brief The slots that hash to one key, compared in full on a lookup. */
    using key_bucket = std::vector<std::uint32_t>;

    struct slot final {
        std::string instance_id;
        std::string account_id;
        surface_row row;
    };

    std::vector<slot> rows_;
    std::vector<std::uint32_t> alive_;
    std::vector<std::uint32_t> generation_;
    std::vector<std::unique_ptr<curve_axis>> axes_;
    std::unordered_map<std::string, std::size_t> by_pillars_;
    std::unordered_map<std::size_t, key_bucket> by_key_;
    std::uint64_t version_ = 0;
};

}

#endif
