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
#ifndef HEDGR_STORE_REPOSITORY_ENTITY_HPP
#define HEDGR_STORE_REPOSITORY_ENTITY_HPP

#include <cstdint>
#include <optional>
#include <sqlgen/PrimaryKey.hpp>
#include <sqlgen/Unique.hpp>
#include <string>

namespace hedgr::store::repository {

/**
 * @brief The stored rows, one struct per table.
 *
 * These are the description sqlgen turns into SQL and into the row mapping, so
 * a column is named once and both follow from it. They are deliberately not the
 * domain types: @c hedgr.model carries no persistence marker and no SQLite type,
 * and a repository is where the two are translated. See ADR 3, which rejected
 * this coupling and ADR 4 and this unit, which reverse that part of it.
 *
 * There is no @c schema member, unlike OreStudio's entities: SQLite has no
 * schemas, so the field would be a lie.
 *
 * A field is written in the order the table's columns are written, because
 * sqlgen binds an insert by the order the fields are declared in and builds the
 * @c SELECT list from the same order.
 *
 * What these structs cannot describe, and what the schema therefore states by
 * hand, is listed beside the schema statements in @c schema.cpp. It is the
 * @c ON DELETE CASCADE on the three tables that cascade, and a column default.
 */

/**
 * @brief Where Hedgr points: @c instance.
 *
 * @c name is unique in the store, so an operator cannot hold two rows for the
 * same deployment under different labels.
 *
 * The three TLS columns hold file paths and never certificate or key material,
 * which is what keeps the one secret of the trio on the filesystem where its
 * permissions apply rather than in a file the operator copies between hosts.
 */
struct instance_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "instance";

    sqlgen::PrimaryKey<std::string> id;
    sqlgen::Unique<std::string> name;
    /**
     * @brief Which illustration the interface draws, as a key and never bytes.
     *
     * The key is the OreStudio environment label, and the picture is an asset
     * of the install rather than a value in this row. Storing the image would
     * put a copy in every instance and in every copy of the store, which is the
     * cost the operator rejected when this column was chosen over a blob.
     *
     * Empty is a stored value like any other and means no illustration, which
     * is what a row created by hand carries.
     */
    std::string icon_key;
    std::string nats_url;
    std::string subject_prefix;
    std::string wire_format;
    std::string folder_id;
    std::string tls_ca;
    std::string tls_cert;
    std::string tls_key;
    std::string created_at;
};

/**
 * @brief A node of an instance's accounts tree: @c folder.
 *
 * @c owner_id is empty for the instances tree and names the instance for an
 * accounts tree, which is what makes "exactly one root per tree per owner"
 * expressible. The index that enforces it is generated from this struct too;
 * see @c schema.cpp.
 */
struct folder_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "folder";

    sqlgen::PrimaryKey<std::string> id;
    std::string parent_id;
    std::string owner_id;
    std::string tree;
    std::string name;
};

/**
 * @brief A declared label: @c label.
 *
 * @c name is unique, so a label is declared once and attached from then on.
 */
struct label_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "label";

    sqlgen::PrimaryKey<std::string> id;
    sqlgen::Unique<std::string> name;
    std::string colour;
    std::string severity;
};

/**
 * @brief The row of @c account as a list reads it and a save writes it.
 *
 * The credential column is deliberately absent, and that is the whole reason
 * this struct and @c account_credential_entity exist as a pair: a list is for
 * rendering, and a list that selected every credential in the store would put
 * every secret on the path a table draws from. A save does not carry the column
 * either, so an edit of an account leaves whatever credential it had alone.
 */
struct account_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "account";

    sqlgen::PrimaryKey<std::string> id;
    std::string instance_id;
    std::string name;
    std::string principal;
    std::string folder_id;

    /**
     * @brief The party this account last acted as, or empty for none yet.
     *
     * A choice rather than a secret, so it belongs on the struct a list reads:
     * the sign-in prompt says which party the next sign-in will land in, and
     * that is read before any window exists.
     */
    std::string party_id;
};

/**
 * @brief The same row of @c account, with the one column a list must not read.
 *
 * A separate struct rather than an extra field on @c account_entity, because
 * the field's absence above is a rule and not an oversight: nothing but the
 * call that is about to use a credential may name this type.
 */
struct account_credential_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "account";

    sqlgen::PrimaryKey<std::string> id;
    std::string instance_id;
    std::string name;
    std::string principal;
    std::string folder_id;
    std::string party_id;
    std::optional<std::string> credential;
};

/**
 * @brief The label attachments of instances: @c instance_label.
 *
 * The pair is the key and there is no surrogate id, which is what makes
 * attaching the same label twice the same attachment rather than two.
 */
struct instance_label_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "instance_label";

    std::string instance_id;
    std::string label_id;
};

/** @brief The label attachments of accounts: @c account_label. */
struct account_label_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "account_label";

    std::string account_id;
    std::string label_id;
};

/**
 * @brief A window description that survives a restart: @c window_spec.
 *
 * @c kind travels beside @c state_json rather than inside it, so no
 * @c std::variant is ever serialised. The session columns are nullable because
 * a window may be bound to no session at all.
 */
struct window_spec_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "window_spec";

    sqlgen::PrimaryKey<std::string> window_id;
    std::string kind;
    std::optional<std::string> session_instance_id;
    std::optional<std::string> session_account_id;
    std::int64_t cadence_ms;
    std::string state_json;
};

/**
 * @brief One entry of the shortcuts bar: @c shortcut.
 *
 * The target is a kind and an id rather than a foreign key, because the bar
 * points at things of several kinds and no one table owns them all. The kind is
 * the window kind's canonical text, so a shortcut to the Instances window
 * itself is expressible and is not forced to name an instance.
 *
 * @c position is the order the bar draws in, and it is a column rather than the
 * rowid because a reorder is a smaller write when it moves one number than when
 * it rewrites the table. Ties are broken by id when the rows are read, so the
 * order is total even if two entries are saved at the same position.
 */
struct shortcut_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "shortcut";

    sqlgen::PrimaryKey<std::string> id;
    std::string target_kind;
    std::string target_id;
    std::int64_t position;
};

/**
 * @brief The single row that proves the master password: @c master.
 *
 * One row, keyed by a literal, holding a known sentence sealed under the master
 * password. Neither the password nor anything derived from it is here.
 */
struct master_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "master";

    sqlgen::PrimaryKey<std::string> id;
    std::string verification;
    std::string created_at;
};

/**
 * @brief The row of @c schema_version that says which build wrote the file.
 *
 * Not a repository's table: it is read to refuse a file this build did not
 * write, which is the only thing the table is for.
 */
struct schema_version_entity final {
    /** @brief The table this struct describes. */
    constexpr static const char* tablename = "schema_version";

    std::int64_t version;
};

}

#endif
