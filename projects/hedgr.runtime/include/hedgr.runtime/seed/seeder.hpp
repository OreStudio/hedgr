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
#ifndef HEDGR_RUNTIME_SEED_SEEDER_HPP
#define HEDGR_RUNTIME_SEED_SEEDER_HPP

#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/folder_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/label_repository.hpp"
#include "hedgr.store/repository/labelling_repository.hpp"
#include <optional>
#include <string>
#include <vector>

namespace hedgr::runtime::seed {

/** @brief The catalogue tables seeding writes into. */
struct tables final {
    model::state::instance_table& instances;
    model::state::account_table& accounts;
    model::state::folder_table& folders;
    model::state::label_table& labels;
    model::state::labelling_table& labellings;
};

/**
 * @brief The repositories seeding writes through.
 *
 * Pointers rather than references because the manager holds them as owners
 * that are empty until the store is open, and the folder and label writes
 * skip the store when there is none.
 */
struct repositories final {
    store::repository::instance_repository* instances = nullptr;
    store::repository::account_repository* accounts = nullptr;
    store::repository::folder_repository* folders = nullptr;
    store::repository::label_repository* labels = nullptr;
    store::repository::labelling_repository* labellings = nullptr;
};

/**
 * @brief Gives a seeded instance back the illustration key a migration
 * dropped.
 *
 * The version 7 to 8 migration added @c icon_key empty on every existing
 * row, because empty is the stored meaning of no illustration chosen and
 * the migration would not guess which rows had one. It runs here, where the
 * store has just been read, rather than in the migration, so it reaches
 * every store rather than only the one that happens to migrate next, and it
 * is idempotent because a row whose key is already set is left alone.
 *
 * It only writes a row whose id carries the seeder's prefix and whose
 * derived key has an asset behind it, so a hand-typed row is never given a
 * picture. A refused write is raised as a condition on the instance.
 */
[[nodiscard]] std::vector<refusal>
backfill_seeded_icon_keys(const tables& state, const repositories& stores);

/**
 * @brief Anchors the folder trees, seeds a first run and the seeded logins.
 *
 * Run once per start over tables already loaded from the store. Every write
 * the store refused is returned in the order it happened, and the caller
 * raises each one.
 */
[[nodiscard]] std::vector<refusal> seed_store(const tables& state,
    const repositories& stores, const session::transport::session_options& seed);

/** @brief Creates the accounts root of one instance, if it has none. */
[[nodiscard]] std::optional<refusal> ensure_accounts_root(
    model::state::folder_table& folders,
    store::repository::folder_repository* folder_repo,
    const std::string& instance_id);

}

#endif
