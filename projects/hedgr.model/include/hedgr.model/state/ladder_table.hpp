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
#ifndef HEDGR_MODEL_STATE_LADDER_TABLE_HPP
#define HEDGR_MODEL_STATE_LADDER_TABLE_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/composite_key.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace hedgr::model::state {

/** @brief Where one ladder's read stands. */
enum class ladder_state : std::uint8_t {
    /** @brief Nobody has asked for it. */
    unread = 0,
    /** @brief A read is out. */
    reading = 1,
    /** @brief The ladder is held. */
    ready = 2,
    /** @brief The read failed; the error says why. */
    failed = 3,
};

/** @brief One ladder as it was last read, and where its read stands. */
struct ladder_row final {
    /** @brief The ladder the deployment stated. */
    domain::rate_ladder ladder;

    /** @brief Why the last read failed, or empty. */
    std::string error;

    /** @brief Where the read stands. */
    ladder_state state = ladder_state::unread;

    /** @brief Bumped by every write, so a view rebuilds only when it moved. */
    std::uint64_t version = 0;
};

/**
 * @brief The key one ladder is held under.
 *
 * The three facts that make a ladder one ladder rather than another. They are
 * joined rather than hashed so a reader of a log line can see which ladder it
 * was, and the separator is a unit separator because a currency or an
 * instrument type never holds one.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string ladder_key(
    std::string_view currency, std::string_view instrument_type,
    std::string_view forward_start);

/**
 * @brief The rate ladders each session has read, keyed by session and ladder.
 *
 * A ladder is read whole and replaced whole: the deployment states its rungs in
 * maturity order, so there is nothing to merge. A failed refresh keeps the
 * ladder it had, because a screen that emptied itself on one timeout would be
 * reporting a deployment that publishes nothing.
 */
class HEDGR_MODEL_EXPORT ladder_table final {
public:
    /** @brief Marks the ladder as in flight, keeping its rungs on screen. */
    void begin(std::string_view instance_id, std::string_view account_id,
        std::string_view key);

    /** @brief Replaces the ladder with the one just read. */
    void apply(std::string_view instance_id, std::string_view account_id,
        std::string_view key, domain::rate_ladder ladder);

    /** @brief Records a failed read, keeping the last good ladder on screen. */
    void fail(std::string_view instance_id, std::string_view account_id,
        std::string_view key, std::string message);

    /** @brief One session's ladder, or nullptr when it has none. */
    [[nodiscard]] const ladder_row* get(std::string_view instance_id,
        std::string_view account_id, std::string_view key) const;

    /** @brief Drops every ladder, for a session teardown. */
    void clear();

private:
    composite_map<3, ladder_row> rows_;

    /** @brief The last version stamped; never reset, so none repeats. */
    std::uint64_t version_ = 0;
};

}

#endif
