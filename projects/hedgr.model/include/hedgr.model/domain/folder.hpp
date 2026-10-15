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
#ifndef HEDGR_MODEL_DOMAIN_FOLDER_HPP
#define HEDGR_MODEL_DOMAIN_FOLDER_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::model::domain {

/**
 * @brief Which tree a folder sits in.
 *
 * The two trees are addressed by the same widget: the Instances window is a
 * folder tree over instances and the Accounts window is a folder tree over the
 * accounts of one instance. They share a shape and nothing else, so the tree is
 * a column rather than a separate type per tree.
 */
enum class folder_tree : std::uint8_t {
    /** @brief The single tree of every instance this process knows. */
    instances = 0,
    /** @brief The tree of one instance's accounts. */
    accounts = 1,
};

/** @brief The canonical text of a tree, as stored in the local database. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(folder_tree value) noexcept;

/** @brief Parses a folder tree from its canonical text. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<folder_tree, std::string>
folder_tree_from_string(std::string_view text);

/**
 * @brief A named container in a tree.
 *
 * A folder belongs to exactly one tree and carries no configuration of its own;
 * what it holds is the set of rows that name it as their parent. Its parent is
 * named by id rather than by handle, because the folder is a persisted value
 * while a handle addresses a row of the live table.
 *
 * @c owner_id is what makes "exactly one root per tree per owner" expressible
 * as a database fact. The instances tree is a single tree with no owner, so its
 * root's @c owner_id is empty. Every instance owns its own accounts tree, so
 * each accounts root is owned by that instance. Two accounts roots may both
 * have an empty @c parent_id precisely because their @c owner_id differs, and
 * the partial unique index over @c (tree, owner_id) where @c parent_id is empty
 * is what refuses a second root in the same tree.
 */
struct folder final {
    /** @brief Stable key, unique in the local store. */
    std::string id;

    /** @brief Key of the containing folder. Empty means this folder is a root. */
    std::string parent_id;

    /** @brief Key of the instance that owns this tree. Empty for the instances
     * tree. */
    std::string owner_id;

    /** @brief Short human label, unique among its siblings. */
    std::string name;

    /** @brief Which tree the folder sits in. */
    folder_tree tree = folder_tree::instances;
};

/** @brief The id of the single root of the instances tree. */
inline constexpr std::string_view instances_root_id = "instances-root";

/**
 * @brief The id of the accounts root owned by @p instance_id.
 *
 * Derived from the instance rather than stored as a constant, because every
 * instance has one and there is exactly one per instance. Deriving it keeps the
 * two in step without a lookup: creating an instance creates this root, and
 * asking for its accounts folder is a string operation, not a store read.
 */
[[nodiscard]] inline std::string accounts_root_id_for(std::string_view instance_id) {
    return "accounts-root:" + std::string{instance_id};
}

}

#endif
