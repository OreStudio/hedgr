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
#ifndef HEDGR_MODEL_STATE_CURVE_TABLE_HPP
#define HEDGR_MODEL_STATE_CURVE_TABLE_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/** @brief Where one published curve stands. */
enum class curve_state : std::uint8_t {
    /** @brief Nobody has asked for it. */
    unread = 0,
    /** @brief A read is out. */
    reading = 1,
    /** @brief The curve is held. */
    ready = 2,
    /** @brief The read failed; the error says why. */
    failed = 3,
};

/**
 * @brief One published curve: its pillars, its instants, and their values.
 *
 * Columnar, because a desk plots many curves at once and a curve is a matrix:
 * the pillars are the same at every instant, so they are stored once and
 * ordered for drawing, and the values are one flat block with a row per
 * instant. A window draws a slice of that block; nothing is copied per frame
 * and nothing is sorted per frame.
 */
/** @brief How many instants of one curve a desk keeps. */
inline constexpr std::size_t curve_history_limit = 32;

/**
 * @brief One ordered set of pillars, shared by every curve that has it.
 *
 * Usually every curve on a desk is quoted on the same pillars, so the names and
 * their tenor order are stored once and addressed by index. A curve's values
 * are already in that order, which is what lets a plot draw N curves without
 * sorting or copying anything per frame.
 *
 * The table holds its axes behind a pointer and never copies one, because
 * @c labels points into @c pillars: a plot hands the labels to the plot library
 * and reads them every frame, so an axis that moved would leave it reading a
 * freed string - a curve drawn against garbage.
 */
struct curve_axis final {
    /** @brief The pillar names, in drawing order. */
    std::vector<std::string> pillars;

    /**
     * @brief The x position of each pillar, from zero, in drawing order.
     *
     * A pillar is a category rather than a number, so the plot's x axis is the
     * pillar's index and its label is the pillar's name. The positions are
     * made once with the axis because a plot reads them every frame.
     */
    std::vector<double> positions;

    /** @brief @c pillars as C strings, index for index, for the plot's axis. */
    std::vector<const char*> labels;
};

struct curve_row final {
    /** @brief The curve's name, as the deployment spells it. */
    std::string label;

    /** @brief The series the curve is published under. */
    std::string series_id;

    /** @brief Why the last read failed, or empty. */
    std::string error;

    /** @brief Which shared pillar axis this curve is quoted on. */
    std::size_t axis = 0;

    /** @brief The instants, oldest first, at most @c curve_history_limit. */
    std::vector<std::int64_t> as_of_ns;

    /** @brief @c as_of_ns.size() * pillars() values, row-major. */
    std::vector<double> values;

    curve_state state = curve_state::unread;

    /** @brief Bumped by every write, so a plot rebuilds only when it moved. */
    std::uint64_t version = 0;

    /** @brief How many instants the curve holds. */
    [[nodiscard]] std::size_t instants() const noexcept {
        return as_of_ns.size();
    }

    /** @brief The value at one instant and pillar, or nothing outside it. */
    [[nodiscard]] const double* instant(std::size_t row,
        std::size_t pillars) const noexcept {
        if (row >= as_of_ns.size() || pillars == 0)
            return nullptr;
        return values.data() + row * pillars;
    }
};

/** @brief A curve's slot in the table. */
struct curve_handle final {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool operator==(const curve_handle&) const = default;
};

/**
 * @brief Every published curve a session holds, keyed by instance, account and
 * series.
 *
 * A curve is interned once and shared: two charts of the same curve address one
 * row, one read and one block of values, which is what makes a desk of many
 * curves cost one read each rather than one per window.
 */
class HEDGR_MODEL_EXPORT curve_table final {
public:
    /** @brief The row's slot, making one when the pair and series is new. */
    [[nodiscard]] curve_handle intern(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id);

    /** @brief The slot for a curve already interned, or nothing. */
    [[nodiscard]] std::optional<curve_handle> find(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id) const;

    [[nodiscard]] bool contains(curve_handle h) const noexcept;
    [[nodiscard]] const curve_row* get(curve_handle h) const noexcept;
    [[nodiscard]] curve_row* get(curve_handle h) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    /** @brief Marks the curve as being read. */
    void begin(curve_handle h);

    /**
     * @brief Stores one read's answer.
     *
     * @param pillars the pillar names, already in drawing order.
     * @param as_of_ns one instant per row of @p values.
     * @param values row-major, @c as_of_ns.size() * @c pillars.size() long.
     */
    void apply(curve_handle h, std::string label,
        std::vector<std::string> pillars, std::vector<std::int64_t> as_of_ns,
        std::vector<double> values);

    /** @brief Records a failed read and keeps whatever was held before. */
    void fail(curve_handle h, std::string message);

    /** @brief Forgets every curve. */
    void clear();

    /** @brief The version stamp of a row, zero when the handle is stale. */
    [[nodiscard]] std::uint64_t version(curve_handle h) const noexcept;

    /** @brief The pillars' order for a set of names: by tenor, then by name. */
    [[nodiscard]] static std::vector<std::size_t>
    pillar_order(const std::vector<std::string>& pillars);

    /** @brief The shared pillar axis a curve is quoted on, by index. */
    [[nodiscard]] const curve_axis& axis(std::size_t index) const noexcept {
        return *axes_[index];
    }

    /** @brief How many distinct pillar axes the held curves use. */
    [[nodiscard]] std::size_t axes() const noexcept { return axes_.size(); }

private:
    /** @brief The shared axis for @p pillars, making one when it is new. */
    [[nodiscard]] std::size_t intern_axis(
        const std::vector<std::string>& pillars);

    /** @brief The slots that hash to one key, compared in full on a lookup. */
    using key_bucket = std::vector<std::uint32_t>;

    struct slot final {
        std::string instance_id;
        std::string account_id;
        curve_row row;
    };

    std::vector<slot> rows_;
    std::vector<std::uint32_t> alive_;
    std::vector<std::uint32_t> generation_;
    std::vector<std::unique_ptr<curve_axis>> axes_;
    std::unordered_map<std::string, std::size_t> by_pillars_;

    /**
     * @brief Slots by the hash of their three names.
     *
     * A lookup happens once per curve per frame, so it must not build a key
     * string on the heap: the names are hashed as views and the few slots that
     * share a hash are compared in full.
     */
    std::unordered_map<std::size_t, key_bucket> by_key_;
    std::uint64_t version_ = 0;
};

}

#endif
