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
#ifndef HEDGR_MODEL_STATE_CRM_TABLE_HPP
#define HEDGR_MODEL_STATE_CRM_TABLE_HPP

#include "hedgr.model/domain/crm.hpp"
#include "hedgr.model/export.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/** @brief The latest cross-rates batch for one session and party. */
struct crm_snapshot final {
    std::vector<domain::crm_cell> cells;
    std::string observed_as_of;
    bool reading = false;
    std::string error;
};

/**
 * @brief The CRM batches each session has read, keyed by session and party.
 *
 * CRM is pull-only in the deployment, so this is the latest batch and not a
 * history. The UI filters by CRM and pair; the table keeps every cell the batch
 * returned, because the rows and columns of the matrix are derived from it.
 */
class HEDGR_MODEL_EXPORT crm_table final {
public:
    /** @brief Marks the batch as in flight, keeping the cells on screen. */
    void begin(std::string_view instance_id, std::string_view account_id,
        std::string_view party_id);
    /** @brief Replaces the batch with the cells just read. */
    void set(std::string_view instance_id, std::string_view account_id,
        std::string_view party_id, std::vector<domain::crm_cell> cells,
        std::string observed_as_of);
    /** @brief Records a failed read and drops the cells it could not refresh. */
    void fail(std::string_view instance_id, std::string_view account_id,
        std::string_view party_id, std::string message);

    /** @brief One session and party's latest batch, or nullptr. */
    [[nodiscard]] const crm_snapshot* get(std::string_view instance_id,
        std::string_view account_id, std::string_view party_id) const;

    /** @brief Drops every batch, for a session teardown. */
    void clear();

private:
    std::unordered_map<std::string, crm_snapshot> snapshots_;
};

}

#endif
