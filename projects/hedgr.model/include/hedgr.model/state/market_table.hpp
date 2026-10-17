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
#ifndef HEDGR_MODEL_STATE_MARKET_TABLE_HPP
#define HEDGR_MODEL_STATE_MARKET_TABLE_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/** @brief Where one session's series catalogue read stands. */
enum class catalogue_state : std::uint8_t {
    /** @brief Nothing has been asked for. */
    unread = 0,
    /** @brief A read is in flight. */
    reading = 1,
    /** @brief The last read succeeded. */
    ready = 2,
    /** @brief The last read failed; the error says why. */
    failed = 3,
};

/** @brief The catalogue one session read. */
struct market_catalogue final {
    /** @brief The series, in the order the chooser shows them. */
    std::vector<domain::market_series_row> series;

    /** @brief Where the read stands. */
    catalogue_state state = catalogue_state::unread;

    /** @brief Why the last read failed, or empty. */
    std::string error;
};

/**
 * @brief The market series each session knows, and the points each chart holds.
 *
 * Everything is keyed by the session, not by the process. Two accounts on two
 * instances are two catalogues: a series is chosen by a login, the list a login
 * may read is its own, and a chart read with one account's token must not be
 * handed to a window signed in as another. The points carry the same pair for
 * the same reason.
 */
class HEDGR_MODEL_EXPORT market_table final {
public:
    /** @brief Records one session's catalogue and marks the read ready. */
    void set_catalogue(std::string_view instance_id,
        std::string_view account_id,
        std::vector<domain::market_series_row> rows);

    /** @brief Marks one session's catalogue read as started. */
    void begin_catalogue(std::string_view instance_id,
        std::string_view account_id);

    /** @brief Records a failed catalogue read for one session. */
    void fail_catalogue(std::string_view instance_id,
        std::string_view account_id, std::string message);

    /** @brief One session's catalogue, or nullptr when it has none. */
    [[nodiscard]] const market_catalogue*
    catalogue(std::string_view instance_id,
        std::string_view account_id) const;

    /** @brief Records one chart's points and marks the read complete. */
    void set_points(std::string_view instance_id, std::string_view account_id,
        std::string_view series_id,
        std::vector<domain::market_point> points, std::int64_t observed_at_ns);

    /** @brief Records a failed points read, keeping the failure visible. */
    void fail_points(std::string_view instance_id, std::string_view account_id,
        std::string_view series_id, std::string message,
        std::int64_t observed_at_ns);

    /** @brief One chart's points, or nullptr when none are held. */
    [[nodiscard]] const domain::market_series_snapshot*
    points(std::string_view instance_id, std::string_view account_id,
        std::string_view series_id) const;

    /** @brief Drops every catalogue and every chart. */
    void clear();

private:
    std::unordered_map<std::string, market_catalogue> catalogues_;
    std::unordered_map<std::string, domain::market_series_snapshot> points_;
};

}

#endif
