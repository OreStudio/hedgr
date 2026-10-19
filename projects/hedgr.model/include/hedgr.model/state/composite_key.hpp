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
#ifndef HEDGR_MODEL_STATE_COMPOSITE_KEY_HPP
#define HEDGR_MODEL_STATE_COMPOSITE_KEY_HPP

#include <boost/container_hash/hash.hpp>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace hedgr::model::state {

/** @brief The parts of a composite key, borrowed from the caller. */
template<std::size_t N>
using key_parts = std::array<std::string_view, N>;

/**
 * @brief A key made of N strings, such as an instance, an account and a series.
 *
 * The frame loop looks rows up by identifiers it already holds, which are UUIDs
 * too long for the small-string buffer. Joining them into one string would
 * allocate on every lookup; a map keyed by this type is searched with
 * @c key_parts instead, which borrows the caller's strings.
 */
template<std::size_t N>
struct composite_key final {
    std::array<std::string, N> parts;

    explicit composite_key(const key_parts<N>& views) {
        for (std::size_t i = 0; i < N; ++i)
            parts[i] = std::string{views[i]};
    }

    [[nodiscard]] key_parts<N> view() const noexcept {
        key_parts<N> result;
        for (std::size_t i = 0; i < N; ++i)
            result[i] = parts[i];
        return result;
    }
};

/**
 * @brief Hashes a key and its borrowed parts alike.
 *
 * Deliberately not noexcept: libstdc++ stores the hash in each node only for a
 * hash that may throw, and without it a lookup rehashes the next node's parts
 * to find where its bucket ends, which costs more than the stored word.
 */
template<std::size_t N>
struct composite_hash final {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const key_parts<N>& parts) const {
        std::size_t seed = 0;
        for (const auto part : parts)
            boost::hash_combine(seed, std::hash<std::string_view>{}(part));
        return seed;
    }

    [[nodiscard]] std::size_t operator()(const composite_key<N>& key) const {
        return (*this)(key.view());
    }
};

/** @brief Compares keys and borrowed parts in any combination. */
template<std::size_t N>
struct composite_equal final {
    using is_transparent = void;

    [[nodiscard]] static key_parts<N> parts_of(const key_parts<N>& parts)
        noexcept {
        return parts;
    }

    [[nodiscard]] static key_parts<N> parts_of(const composite_key<N>& key)
        noexcept {
        return key.view();
    }

    template<class A, class B>
    [[nodiscard]] bool operator()(const A& a, const B& b) const noexcept {
        return parts_of(a) == parts_of(b);
    }
};

/** @brief A hash map keyed by N strings and searched without allocating. */
template<std::size_t N, class Value>
using composite_map = std::unordered_map<composite_key<N>, Value,
    composite_hash<N>, composite_equal<N>>;

/** @brief The value under @p parts, or nullptr. */
template<std::size_t N, class Value>
[[nodiscard]] const Value* find_in(const composite_map<N, Value>& map,
    const key_parts<N>& parts) {
    const auto found = map.find(parts);
    return found == map.end() ? nullptr : &found->second;
}

/** @brief The value under @p parts, inserted empty when absent. */
template<std::size_t N, class Value>
[[nodiscard]] Value& slot_in(composite_map<N, Value>& map,
    const key_parts<N>& parts) {
    if (const auto found = map.find(parts); found != map.end())
        return found->second;
    return map.try_emplace(composite_key<N>{parts}).first->second;
}

}

#endif
