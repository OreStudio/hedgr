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

/** @brief Where the series catalogue's last read stands. */
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

/**
 * @brief The market series this process knows, and their plotted points.
 *
 * The catalogue is one list because a chooser shows one list. The points are
 * per series because every chart owns its own window and its own time range,
 * and two charts of two series must not wait on each other.
 */
class HEDGR_MODEL_EXPORT market_table final {
public:
    /** @brief Records the catalogue and marks the read ready. */
    void set_catalogue(std::vector<domain::market_series_row> rows);

    /** @brief Marks the catalogue read as started. */
    void begin_catalogue();

    /** @brief Records a failed catalogue read. */
    void fail_catalogue(std::string message);

    /** @brief The series, in the order the chooser shows them. */
    [[nodiscard]] const std::vector<domain::market_series_row>& series() const
        noexcept {
        return series_;
    }

    /** @brief Where the catalogue read stands. */
    [[nodiscard]] catalogue_state catalogue() const noexcept {
        return catalogue_;
    }

    /** @brief Why the last catalogue read failed, or empty. */
    [[nodiscard]] const std::string& catalogue_error() const noexcept {
        return catalogue_error_;
    }

    /** @brief Records one series' points and marks the read complete. */
    void set_points(std::string_view series_id,
        std::vector<domain::market_point> points, std::int64_t observed_at_ns);

    /** @brief Records a failed points read, keeping the failure visible. */
    void fail_points(std::string_view series_id, std::string message,
        std::int64_t observed_at_ns);

    /** @brief The points of one series, or nullptr when none are held. */
    [[nodiscard]] const domain::market_series_snapshot*
    points(std::string_view series_id) const;

    /** @brief Drops the catalogue and every series' points. */
    void clear();

private:
    std::vector<domain::market_series_row> series_;
    catalogue_state catalogue_ = catalogue_state::unread;
    std::string catalogue_error_;
    std::unordered_map<std::string, domain::market_series_snapshot> points_;
};

}

#endif
