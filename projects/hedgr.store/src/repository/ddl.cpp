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
#include "hedgr.store/repository/ddl.hpp"
#include "hedgr.store/repository/entity.hpp"
#include <sqlgen/create_index.hpp>
#include <sqlgen/literals.hpp>
#include <sqlgen/where.hpp>

namespace hedgr::store::repository {

namespace {

using namespace sqlgen;
using namespace sqlgen::literals;

/**
 * @brief The three tables sqlgen cannot define, and exactly what it cannot say.
 *
 * Every other table in this store is the statement sqlgen derives from its
 * entity struct, so its columns exist once. These three are written out by hand
 * and the reason is one clause that sqlgen does not emit.
 *
 * @c ON DELETE CASCADE. sqlgen's @c ForeignKey writes @c REFERENCES
 * "table"("column") and stops there -- its SQLite dialect has no cascade
 * property at all. A reference without the cascade is not a weaker version of
 * this constraint, it is a different one: removing an instance would be refused
 * while it still had accounts, and the operator would have to empty it by hand
 * in an order the interface does not offer. Losing the cascade to gain symmetry
 * would be trading a working store for a tidier diff.
 *
 * @c account therefore also carries @c UNIQUE(instance_id, @c principal) here.
 * sqlgen describes uniqueness a column at a time, and this one is a pair: the
 * same principal on two instances is two accounts, and the same principal twice
 * on one instance is not.
 *
 * @c instance_label and @c account_label carry @c PRIMARY KEY (owner, @c label)
 * for the same reason. The pair is the identity, which is what makes attaching
 * a label twice the same attachment. Two per-column uniques would forbid what
 * the store is for.
 *
 * The columns here are the columns of the matching entity structs, in the same
 * order. That they agree is checked by reading every entity back from the
 * applied schema, in @c store_tests.
 */
const std::vector<table_ddl> hand_written{
    {
        .name = account_entity::tablename,
        .text = R"(CREATE TABLE IF NOT EXISTS account (
        id TEXT PRIMARY KEY,
        instance_id TEXT NOT NULL REFERENCES instance(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        principal TEXT NOT NULL,
        folder_id TEXT NOT NULL,
        credential TEXT,
        UNIQUE(instance_id, principal)
    ))",
    },
    {
        .name = instance_label_entity::tablename,
        .text = R"(CREATE TABLE IF NOT EXISTS instance_label (
        instance_id TEXT NOT NULL REFERENCES instance(id) ON DELETE CASCADE,
        label_id TEXT NOT NULL REFERENCES label(id) ON DELETE CASCADE,
        PRIMARY KEY (instance_id, label_id)
    ))",
    },
    {
        .name = account_label_entity::tablename,
        .text = R"(CREATE TABLE IF NOT EXISTS account_label (
        account_id TEXT NOT NULL REFERENCES account(id) ON DELETE CASCADE,
        label_id TEXT NOT NULL REFERENCES label(id) ON DELETE CASCADE,
        PRIMARY KEY (account_id, label_id)
    ))",
    },
};

/**
 * @brief The partial unique index that makes the single root a database fact.
 *
 * sqlgen can express this one after all: @c create_unique_index takes the
 * columns and a @c WHERE clause, which is exactly a partial index. It is
 * generated rather than written out because the columns it covers are columns
 * of @c folder_entity, and a hand-written copy could name a column that had
 * been renamed.
 *
 * At most one row per (tree, owner_id) may have no parent, so a second root in
 * the instances tree, or a second accounts root for one instance, is refused by
 * SQLite even if a caller never checks first.
 */
std::vector<std::string> make_indexes() {
    return {
        sqlite::to_sql(create_unique_index<"folder_root", folder_entity>(
                           "tree"_c, "owner_id"_c) |
                       if_not_exists |
                       where("parent_id"_c == std::string{""})),
    };
}

/**
 * @brief Every table, generated where sqlgen can and hand-written where it
 * cannot.
 *
 * The hand-written three are spliced into their place in the order rather than
 * appended, because the order is a creation order: @c account references
 * @c instance, and both label junctions reference @c label.
 */
std::vector<table_ddl> make_tables() {
    std::vector<table_ddl> result;
    result.push_back({.name = instance_entity::tablename,
                      .text = create_table_sql<instance_entity>()});
    result.push_back(hand_written[0]);
    result.push_back({.name = folder_entity::tablename,
                      .text = create_table_sql<folder_entity>()});
    result.push_back({.name = label_entity::tablename,
                      .text = create_table_sql<label_entity>()});
    result.push_back(hand_written[1]);
    result.push_back(hand_written[2]);
    result.push_back({.name = window_spec_entity::tablename,
                      .text = create_table_sql<window_spec_entity>()});
    result.push_back({.name = shortcut_entity::tablename,
                      .text = create_table_sql<shortcut_entity>()});
    result.push_back({.name = schema_version_entity::tablename,
                      .text = create_table_sql<schema_version_entity>()});
    result.push_back({.name = master_entity::tablename,
                      .text = create_table_sql<master_entity>()});
    return result;
}

}

const std::vector<table_ddl>& tables() {
    static const std::vector<table_ddl> value = make_tables();
    return value;
}

std::string ddl_for(std::string_view table) {
    for (const auto& entry : tables()) {
        if (entry.name == table)
            return entry.text;
    }
    return {};
}

const std::vector<std::string>& indexes() {
    static const std::vector<std::string> value = make_indexes();
    return value;
}

}
