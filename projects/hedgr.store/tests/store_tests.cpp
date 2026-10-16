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
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.store/repository/account_repository.hpp"
#include "hedgr.store/repository/entity.hpp"
#include "hedgr.store/repository/folder_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/label_repository.hpp"
#include "hedgr.store/repository/labelling_repository.hpp"
#include "hedgr.store/repository/master_repository.hpp"
#include "hedgr.store/repository/schema.hpp"
#include "hedgr.store/repository/shortcut_repository.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <sqlgen/read.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

using hedgr::model::domain::account;
using hedgr::model::domain::chart_state;
using hedgr::model::domain::folder;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::label;
using hedgr::model::domain::label_colour;
using hedgr::model::domain::label_severity;
using hedgr::model::domain::labelling;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::session_key;
using hedgr::model::domain::shortcut;
using hedgr::model::domain::window_kind;
using hedgr::model::domain::window_spec;
using hedgr::store::repository::account_repository;
using hedgr::store::repository::folder_repository;
using hedgr::store::repository::instance_repository;
using hedgr::store::repository::label_repository;
using hedgr::store::repository::labelling_repository;
using hedgr::store::repository::master_repository;
using hedgr::store::repository::shortcut_repository;
using hedgr::store::repository::window_repository;
using hedgr::store::sqlite::database;
using hedgr::store::sqlite::store_errc;

/** @brief An open in-memory store with the schema already applied. */
database fresh_store() {
    auto opened = database::open_in_memory();
    REQUIRE(opened.has_value());
    database db = std::move(opened).value();
    REQUIRE(db.apply_schema().has_value());
    return db;
}

/**
 * @brief A database file that deletes itself, sidecars included.
 *
 * The schema tests need a file rather than a private in-memory database,
 * because the point of a version check is what a file carries between opens.
 * The name carries a per-run token and a serial, so a file left behind by a
 * killed run can never be picked up by the next one.
 */
class temporary_database final {
public:
    temporary_database()
        : path_(std::filesystem::temp_directory_path() /
              std::format("hedgr.store.{}.{}.sqlite", unique_token(),
                  next_serial())) {
        erase();
    }

    ~temporary_database() {
        erase();
    }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    static std::string unique_token() {
        std::random_device source;
        return std::format("{:08x}{:08x}", source(), source());
    }

    static unsigned next_serial() {
        static std::atomic<unsigned> serial{0};
        return serial.fetch_add(1);
    }

    void erase() const {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }

    std::filesystem::path path_;
};

instance sample_instance() {
    instance row;
    row.id = "inst-1";
    row.name = "Local";
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = "ores.local";
    row.wire_format = "msgpack";
    return row;
}

/**
 * @brief The version 7 instance table, kept here as a literal.
 *
 * Production no longer carries the version 7 statement, and resurrecting it
 * would leave a dead copy of the old schema in the component for the test's
 * sake. The literal is the shape the developer store held before the icon key
 * was added, so the migration runs against the real old table rather than
 * against the current table wearing an old version number.
 */
constexpr std::string_view version_seven_instance_ddl = R"(CREATE TABLE instance (
        id TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL UNIQUE,
        nats_url TEXT NOT NULL,
        subject_prefix TEXT NOT NULL,
        wire_format TEXT NOT NULL,
        folder_id TEXT NOT NULL,
        tls_ca TEXT NOT NULL,
        tls_cert TEXT NOT NULL,
        tls_key TEXT NOT NULL,
        created_at TEXT NOT NULL
    ))";

/**
 * @brief Writes a version 7 store with two instance rows into an open file.
 *
 * Only the two tables the migration touches are created. The rest arrive from
 * apply_schema after the migration, which is exactly what an operator's open
 * does, so the test exercises the real path rather than a hand-built replica of
 * the current schema.
 */
void plant_version_seven_store(database& db) {
    REQUIRE(db.execute(version_seven_instance_ddl).has_value());
    REQUIRE(db.execute("CREATE TABLE schema_version (version INTEGER NOT NULL)")
                .has_value());
    REQUIRE(db.execute("INSERT INTO schema_version (version) VALUES (7)")
                .has_value());

    const auto planted = db.execute(
        "INSERT INTO instance "
        "(id, name, nats_url, subject_prefix, wire_format, folder_id, "
        "tls_ca, tls_cert, tls_key, created_at) VALUES "
        "('inst-1', 'Local', 'nats://localhost:20405', 'ores.local', 'msgpack', "
        "'', '', '', '', '2026-01-01T00:00:00Z'), "
        "('inst-2', 'Staging', 'nats://localhost:20605', 'ores.staging', 'json', "
        "'', '', '', '', '2026-01-02T00:00:00Z')");
    REQUIRE(planted.has_value());
}

}

TEST_CASE("applying the schema twice succeeds", "[store]") {
    auto db = database::open_in_memory();
    REQUIRE(db.has_value());

    REQUIRE(db.value().apply_schema().has_value());
    REQUIRE(db.value().apply_schema().has_value());
}

TEST_CASE("an instance round-trips through save and list", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    const auto saved = repository.save(sample_instance());
    REQUIRE(saved.has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);

    const auto& row = rows.value().front();
    REQUIRE(row.id == "inst-1");
    REQUIRE(row.name == "Local");
    REQUIRE(row.nats_url == "nats://localhost:20405");
    REQUIRE(row.subject_prefix == "ores.local");
    REQUIRE(row.wire_format == "msgpack");
}

TEST_CASE("an instance's TLS paths round-trip, empty ones included", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    /*
     * Two rows in one case, because the empty spelling is a stored value and
     * not an absent one: a row written before these columns existed reads back
     * as three empty strings, and that is the state the runtime falls back to
     * the environment on. A test that only saved paths would leave the
     * fallback untested, and one that only saved empties would not notice the
     * columns being dropped.
     */
    instance carrying = sample_instance();
    carrying.tls_ca = "../../keys/nats/ca.crt";
    carrying.tls_cert = "keys/nats/ores.shell.crt";
    carrying.tls_key = "/home/operator/private/ores.shell.key";
    REQUIRE(repository.save(carrying).has_value());

    instance falling_back = sample_instance();
    falling_back.id = "inst-2";
    falling_back.name = "No material";
    REQUIRE(repository.save(falling_back).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 2);

    /* Ordered by name: "Local" before "No material". */
    const auto& stored = rows.value().front();
    REQUIRE(stored.id == "inst-1");
    REQUIRE(stored.tls_ca == "../../keys/nats/ca.crt");
    REQUIRE(stored.tls_cert == "keys/nats/ores.shell.crt");
    REQUIRE(stored.tls_key == "/home/operator/private/ores.shell.key");

    const auto& empty = rows.value().back();
    REQUIRE(empty.id == "inst-2");
    REQUIRE(empty.tls_ca.empty());
    REQUIRE(empty.tls_cert.empty());
    REQUIRE(empty.tls_key.empty());
}

TEST_CASE("an instance's icon key round-trips, an empty one included", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    /*
     * The key is a stored value and not an absent one, so the empty spelling is
     * round-tripped as well: an instance typed into the form has no environment
     * behind it and therefore no label to key an asset by, and the store has to
     * give that row back saying exactly that rather than inventing a picture.
     */
    instance keyed = sample_instance();
    keyed.icon_key = "brave_hopper";
    REQUIRE(repository.save(keyed).has_value());

    instance unkeyed = sample_instance();
    unkeyed.id = "inst-2";
    unkeyed.name = "Typed by hand";
    REQUIRE(repository.save(unkeyed).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 2);

    /* Ordered by name: "Local" before "Typed by hand". */
    REQUIRE(rows.value().front().icon_key == "brave_hopper");
    REQUIRE(rows.value().back().icon_key.empty());
}

TEST_CASE("a shortcut round-trips and comes back in its order", "[store]") {
    auto db = fresh_store();
    shortcut_repository shortcuts(db);

    /*
     * Saved out of order on purpose: the list has to return them by position
     * rather than by the sequence the writes happened in, because the order of
     * the bar is what the operator arranged and not what the store happened to
     * be told first.
     */
    shortcut last;
    last.id = "sc-last";
    last.target_kind = window_kind::instance_manager;
    last.position = 30;
    REQUIRE(shortcuts.save(last).has_value());

    shortcut first;
    first.id = "sc-first";
    first.target_kind = window_kind::accounts;
    first.target_id = "inst-1";
    first.position = 10;
    REQUIRE(shortcuts.save(first).has_value());

    shortcut middle;
    middle.id = "sc-middle";
    middle.target_kind = window_kind::grid;
    middle.position = 20;
    REQUIRE(shortcuts.save(middle).has_value());

    const auto rows = shortcuts.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 3);

    REQUIRE(rows.value()[0].id == "sc-first");
    REQUIRE(rows.value()[0].target_kind == window_kind::accounts);
    REQUIRE(rows.value()[0].target_id == "inst-1");
    REQUIRE(rows.value()[0].position == 10);

    REQUIRE(rows.value()[1].id == "sc-middle");
    REQUIRE(rows.value()[2].id == "sc-last");

    /* The Instances window is a target with no id behind it. */
    REQUIRE(rows.value()[2].target_kind == window_kind::instance_manager);
    REQUIRE(rows.value()[2].target_id.empty());
}

TEST_CASE("a shortcut target that is removed takes its entries with it",
    "[store]") {
    auto db = fresh_store();
    shortcut_repository shortcuts(db);

    /*
     * This is the call the runtime's cascade makes when a target row goes. The
     * shortcut table cannot carry a foreign key to the target, because the
     * target is a kind and an id that name several tables, so the store offers
     * the removal and the runtime asks for it.
     */
    shortcut to_instance;
    to_instance.id = "sc-instance";
    to_instance.target_kind = window_kind::accounts;
    to_instance.target_id = "inst-1";
    to_instance.position = 10;
    REQUIRE(shortcuts.save(to_instance).has_value());

    shortcut to_other;
    to_other.id = "sc-other";
    to_other.target_kind = window_kind::accounts;
    to_other.target_id = "inst-2";
    to_other.position = 20;
    REQUIRE(shortcuts.save(to_other).has_value());

    REQUIRE(shortcuts.remove_for_target(window_kind::accounts, "inst-1")
                .has_value());

    const auto rows = shortcuts.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);
    REQUIRE(rows.value().front().id == "sc-other");
}

TEST_CASE("saving the same id twice updates the row", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    REQUIRE(repository.save(sample_instance()).has_value());

    instance updated = sample_instance();
    updated.name = "Local Renamed";
    REQUIRE(repository.save(updated).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);
    REQUIRE(rows.value().front().name == "Local Renamed");
}

TEST_CASE("removing an instance cascades to its accounts", "[store]") {
    auto db = fresh_store();
    instance_repository instances(db);
    account_repository accounts(db);

    REQUIRE(instances.save(sample_instance()).has_value());

    account row;
    row.id = "id-1";
    row.instance_id = "inst-1";
    row.name = "ops read-only";
    row.principal = "ops@host";
    REQUIRE(accounts.save(row).has_value());
    REQUIRE(accounts.list_for_instance("inst-1").value().size() == 1);

    REQUIRE(instances.remove("inst-1").has_value());

    const auto remaining = accounts.list_for_instance("inst-1");
    REQUIRE(remaining.has_value());
    REQUIRE(remaining.value().empty());
}

TEST_CASE("a window bound to a session round-trips", "[store]") {
    auto db = fresh_store();
    window_repository windows(db);

    window_spec spec;
    spec.id = "win-session";
    spec.kind = window_kind::chart;
    spec.session = session_key{"inst-1", "id-1"};
    spec.cadence = std::chrono::milliseconds(250);
    spec.state = chart_state{.label = "Trades", .series_subject = "trades",
                             .max_points = 600};
    REQUIRE(windows.save(spec).has_value());

    const auto rows = windows.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);

    const auto& stored = rows.value().front();
    REQUIRE(stored.session.has_value());
    REQUIRE(stored.session->instance_id == "inst-1");
    REQUIRE(stored.session->account_id == "id-1");
    REQUIRE(stored.cadence == std::chrono::milliseconds(250));
}

TEST_CASE("a window with no session comes back unbound", "[store]") {
    auto db = fresh_store();
    window_repository windows(db);

    window_spec spec;
    spec.id = "win-manager";
    spec.kind = window_kind::notice;
    spec.session = std::nullopt;
    REQUIRE(windows.save(spec).has_value());

    const auto rows = windows.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);
    REQUIRE(rows.value().front().session == std::nullopt);
}

TEST_CASE("a chart window's state payload round-trips", "[store]") {
    auto db = fresh_store();
    window_repository windows(db);

    window_spec spec;
    spec.id = "win-chart";
    spec.kind = window_kind::chart;
    spec.state = chart_state{.label = "Latency",
                             .series_subject = "metrics.latency",
                             .max_points = 450};
    REQUIRE(windows.save(spec).has_value());

    const auto rows = windows.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);

    const auto& stored = rows.value().front();
    REQUIRE(stored.kind == window_kind::chart);
    const auto* chart = std::get_if<chart_state>(&stored.state);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->label == "Latency");
    REQUIRE(chart->series_subject == "metrics.latency");
    REQUIRE(chart->max_points == 450);
}

TEST_CASE("an unknown window kind in a row is reported as a corrupt row",
    "[store]") {
    auto db = fresh_store();
    window_repository repository(db);

    const auto planted = db.execute(
        "INSERT INTO window_spec (window_id, kind, cadence_ms, state_json) "
        "VALUES ('win-bad', 'hologram', 1000, '{}')");
    REQUIRE(planted.has_value());

    const auto rows = repository.list();
    REQUIRE_FALSE(rows.has_value());
    REQUIRE(rows.error().code == store_errc::corrupt_row);
    REQUIRE(rows.error().message.contains("win-bad"));
}

TEST_CASE("a nested folder round-trips through save and list", "[store]") {
    auto db = fresh_store();
    folder_repository repository(db);

    folder root;
    root.id = "instances-root";
    root.name = "Instances";
    root.tree = folder_tree::instances;
    REQUIRE(repository.save(root).has_value());

    folder child;
    child.id = "fold-staging";
    child.parent_id = "instances-root";
    child.name = "Staging";
    child.tree = folder_tree::instances;
    REQUIRE(repository.save(child).has_value());

    folder grandchild;
    grandchild.id = "fold-emea";
    grandchild.parent_id = "fold-staging";
    grandchild.name = "EMEA";
    grandchild.tree = folder_tree::instances;
    REQUIRE(repository.save(grandchild).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 3);

    /* Ordered by tree, then owner, then name. */
    REQUIRE(rows.value()[0].id == "fold-emea");
    REQUIRE(rows.value()[0].parent_id == "fold-staging");
    REQUIRE(rows.value()[0].name == "EMEA");
    REQUIRE(rows.value()[0].tree == folder_tree::instances);

    REQUIRE(rows.value()[1].id == "instances-root");
    REQUIRE(rows.value()[1].name == "Instances");
    REQUIRE(rows.value()[1].parent_id == "");

    REQUIRE(rows.value()[2].id == "fold-staging");
    REQUIRE(rows.value()[2].parent_id == "instances-root");

    /* An accounts tree keeps the owner that distinguishes it. */
    folder accounts_root;
    accounts_root.id = "accounts-root:inst-1";
    accounts_root.name = "Accounts";
    accounts_root.owner_id = "inst-1";
    accounts_root.tree = folder_tree::accounts;
    REQUIRE(repository.save(accounts_root).has_value());

    const auto all = repository.list();
    REQUIRE(all.has_value());
    REQUIRE(all.value().size() == 4);
    REQUIRE(all.value()[0].tree == folder_tree::accounts);
    REQUIRE(all.value()[0].owner_id == "inst-1");
}

TEST_CASE("a second root in one tree is refused by the unique index",
    "[store]") {
    auto db = fresh_store();
    folder_repository repository(db);

    folder root;
    root.id = "instances-root";
    root.name = "Instances";
    root.tree = folder_tree::instances;
    REQUIRE(repository.save(root).has_value());

    /*
     * A root has no parent, and the partial unique index covers exactly those
     * rows, so a second one in the same ownerless tree is a constraint failure
     * rather than a second tree.
     */
    folder second;
    second.id = "instances-root-2";
    second.name = "Instances again";
    second.tree = folder_tree::instances;
    const auto refused = repository.save(second);

    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == store_errc::constraint_violated);

    REQUIRE(repository.list().value().size() == 1);

    /* A non-root folder in the same tree is not covered by the index. */
    folder child;
    child.id = "fold-staging";
    child.parent_id = "instances-root";
    child.name = "Staging";
    child.tree = folder_tree::instances;
    REQUIRE(repository.save(child).has_value());
    REQUIRE(repository.list().value().size() == 2);
}

TEST_CASE("a label round-trips its colour and severity", "[store]") {
    auto db = fresh_store();
    label_repository repository(db);

    label row;
    row.id = "lbl-prod";
    row.name = "production";
    row.colour = label_colour::red;
    row.severity = label_severity::danger;
    REQUIRE(repository.save(row).has_value());

    label other;
    other.id = "lbl-dev";
    other.name = "development";
    other.colour = label_colour::blue;
    other.severity = label_severity::none;
    REQUIRE(repository.save(other).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 2);

    REQUIRE(rows.value()[0].id == "lbl-dev");
    REQUIRE(rows.value()[0].colour == label_colour::blue);
    REQUIRE(rows.value()[0].severity == label_severity::none);

    REQUIRE(rows.value()[1].id == "lbl-prod");
    REQUIRE(rows.value()[1].name == "production");
    REQUIRE(rows.value()[1].colour == label_colour::red);
    REQUIRE(rows.value()[1].severity == label_severity::danger);
}

TEST_CASE("deleting an instance cascades to its instance_label rows",
    "[store]") {
    auto db = fresh_store();
    instance_repository instances(db);
    label_repository labels(db);
    labelling_repository labellings(db);

    REQUIRE(instances.save(sample_instance()).has_value());

    label row;
    row.id = "lbl-prod";
    row.name = "production";
    row.colour = label_colour::red;
    row.severity = label_severity::danger;
    REQUIRE(labels.save(row).has_value());

    labelling attachment;
    attachment.owner = labelling_owner::instance;
    attachment.owner_id = "inst-1";
    attachment.label_id = "lbl-prod";
    REQUIRE(labellings.save(attachment).has_value());

    REQUIRE(labellings.list().value().size() == 1);

    REQUIRE(instances.remove("inst-1").has_value());

    const auto remaining = labellings.list();
    REQUIRE(remaining.has_value());
    REQUIRE(remaining.value().empty());

    /* The label itself is untouched: only the attachment named the instance. */
    REQUIRE(labels.list().value().size() == 1);
}

TEST_CASE("a label attachment is idempotent and detachable", "[store]") {
    auto db = fresh_store();
    instance_repository instances(db);
    label_repository labels(db);
    labelling_repository labellings(db);

    REQUIRE(instances.save(sample_instance()).has_value());
    label row;
    row.id = "lbl-prod";
    row.name = "production";
    REQUIRE(labels.save(row).has_value());

    labelling attachment;
    attachment.owner = labelling_owner::instance;
    attachment.owner_id = "inst-1";
    attachment.label_id = "lbl-prod";
    REQUIRE(labellings.save(attachment).has_value());
    REQUIRE(labellings.save(attachment).has_value());
    REQUIRE(labellings.list().value().size() == 1);

    REQUIRE(labellings.remove(attachment).has_value());
    REQUIRE(labellings.list().value().empty());
}

TEST_CASE("an unrecognised severity in a row is reported as a corrupt row",
    "[store]") {
    auto db = fresh_store();
    label_repository repository(db);

    const auto planted = db.execute(
        "INSERT INTO label (id, name, colour, severity) "
        "VALUES ('lbl-bad', 'mystery', 'amber', 'catastrophic')");
    REQUIRE(planted.has_value());

    const auto rows = repository.list();
    REQUIRE_FALSE(rows.has_value());
    REQUIRE(rows.error().code == store_errc::corrupt_row);
    REQUIRE(rows.error().message.contains("lbl-bad"));
    REQUIRE(rows.error().message.contains("catastrophic"));
}

TEST_CASE("an unrecognised tree in a folder row is reported as a corrupt row",
    "[store]") {
    auto db = fresh_store();
    folder_repository repository(db);

    const auto planted = db.execute(
        "INSERT INTO folder (id, parent_id, owner_id, tree, name) "
        "VALUES ('fold-bad', 'instances-root', '', 'hierarchy', 'Broken')");
    REQUIRE(planted.has_value());

    const auto rows = repository.list();
    REQUIRE_FALSE(rows.has_value());
    REQUIRE(rows.error().code == store_errc::corrupt_row);
    REQUIRE(rows.error().message.contains("fold-bad"));
    REQUIRE(rows.error().message.contains("hierarchy"));
}

TEST_CASE("an unrecognised colour in a label row is reported as a corrupt row",
    "[store]") {
    auto db = fresh_store();
    label_repository repository(db);

    const auto planted = db.execute(
        "INSERT INTO label (id, name, colour, severity) "
        "VALUES ('lbl-bad', 'mystery', 'chartreuse', 'danger')");
    REQUIRE(planted.has_value());

    const auto rows = repository.list();
    REQUIRE_FALSE(rows.has_value());
    REQUIRE(rows.error().code == store_errc::corrupt_row);
    REQUIRE(rows.error().message.contains("lbl-bad"));
    REQUIRE(rows.error().message.contains("chartreuse"));
}

TEST_CASE("a file written with another schema version is refused", "[store]") {
    const temporary_database scratch;

    /*
     * The first open creates the file and stamps the version this build writes,
     * which is exactly the state a database written by a build with different
     * columns is in.
     */
    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        REQUIRE(opened.value().apply_schema().has_value());
        REQUIRE(opened.value().execute("UPDATE schema_version SET version = 2")
                    .has_value());
    }

    /*
     * Every table statement is IF NOT EXISTS, so without a version check this
     * second open would succeed and leave the old columns in place. It is the
     * version that turns that into a failure at the point of cause.
     */
    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());
    const auto applied = reopened.value().apply_schema();

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().code == store_errc::schema_mismatch);
    REQUIRE(applied.error().message.contains("2"));
    REQUIRE(applied.error().message.contains(
        std::to_string(hedgr::store::repository::schema_version())));
    REQUIRE(applied.error().message.contains(scratch.path().string()));
    REQUIRE(applied.error().message.contains("delete the file"));

    /*
     * The file is refused rather than destroyed: it holds an operator's
     * instances, so it is still there and still says what it said.
     */
    REQUIRE(std::filesystem::exists(scratch.path()));

    /*
     * A file from a build newer than this one is refused as well, and it is not
     * dragged through the version 7 migration on the way: a version above the
     * current one means the file knows columns this build does not, so reading
     * it as older would corrupt it.
     */
    REQUIRE(reopened.value().execute("UPDATE schema_version SET version = 9")
                .has_value());
    const auto from_the_future = reopened.value().apply_schema();
    REQUIRE_FALSE(from_the_future.has_value());
    REQUIRE(from_the_future.error().code == store_errc::schema_mismatch);
    REQUIRE(from_the_future.error().message.contains("9"));
    REQUIRE(from_the_future.error().message.contains("delete the file"));

    /* A file this build wrote opens without complaint. */
    const temporary_database current;
    auto fresh = database::open(current.path());
    REQUIRE(fresh.has_value());
    REQUIRE(fresh.value().apply_schema().has_value());
    REQUIRE(fresh.value().apply_schema().has_value());
}

TEST_CASE("a version 7 store is migrated and keeps its rows", "[store]") {
    using hedgr::store::repository::schema_version_entity;

    const temporary_database scratch;
    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        plant_version_seven_store(opened.value());
    }

    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());
    database& db = reopened.value();

    REQUIRE(db.apply_schema().has_value());

    /*
     * The stamp moves with the schema. It is compared with the build's own
     * version rather than with a literal, so a future bump that forgets the
     * migration fails here instead of passing on a stale number.
     */
    const auto versions =
        sqlgen::read<std::vector<schema_version_entity>>(db.connection());
    REQUIRE(versions.has_value());
    REQUIRE(versions.value().size() == 1);
    REQUIRE(versions.value().front().version ==
            hedgr::store::repository::schema_version());

    /* The rows are still there, ordered by name, with the empty key. */
    instance_repository repository(db);
    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 2);
    REQUIRE(rows.value()[0].id == "inst-1");
    REQUIRE(rows.value()[0].name == "Local");
    REQUIRE(rows.value()[0].nats_url == "nats://localhost:20405");
    REQUIRE(rows.value()[0].subject_prefix == "ores.local");
    REQUIRE(rows.value()[0].icon_key.empty());
    REQUIRE(rows.value()[1].id == "inst-2");
    REQUIRE(rows.value()[1].name == "Staging");
    REQUIRE(rows.value()[1].icon_key.empty());

    /*
     * The new column is not merely present. A key saved through the repository
     * reads back, so the added column is bound by the same code as a fresh
     * table's.
     */
    instance keyed = sample_instance();
    keyed.id = "inst-3";
    keyed.name = "Keyed";
    keyed.icon_key = "brave_hopper";
    REQUIRE(repository.save(keyed).has_value());
    const auto after = repository.list();
    REQUIRE(after.has_value());
    REQUIRE(after.value().size() == 3);
    REQUIRE(after.value().front().name == "Keyed");
    REQUIRE(after.value().front().icon_key == "brave_hopper");

    /* And the new table is usable through its own repository. */
    shortcut_repository shortcuts(db);
    shortcut entry;
    entry.id = "sc-1";
    entry.target_kind = window_kind::instance_manager;
    entry.position = 10;
    REQUIRE(shortcuts.save(entry).has_value());
    const auto bar = shortcuts.list();
    REQUIRE(bar.has_value());
    REQUIRE(bar.value().size() == 1);
    REQUIRE(bar.value().front().id == "sc-1");

    /*
     * A second open finds the file already at the current version, so it
     * migrates nothing, adds no second version row and leaves the rows alone.
     */
    REQUIRE(db.apply_schema().has_value());
    const auto versions_again =
        sqlgen::read<std::vector<schema_version_entity>>(db.connection());
    REQUIRE(versions_again.has_value());
    REQUIRE(versions_again.value().size() == 1);
    REQUIRE(versions_again.value().front().version ==
            hedgr::store::repository::schema_version());
    REQUIRE(repository.list().value().size() == 3);
    REQUIRE(shortcuts.list().value().size() == 1);
}

TEST_CASE("a store below the migration cut-off is refused unmigrated",
    "[store]") {
    using hedgr::store::repository::schema_version_entity;

    const temporary_database scratch;
    {
        auto opened = database::open(scratch.path());
        REQUIRE(opened.has_value());
        plant_version_seven_store(opened.value());

        /*
         * Version 6 is the nearest store below the one this build migrates, so
         * it is the boundary of the cut-off. The file has no icon key and must
         * be refused rather than pushed through the version 7 step.
         */
        REQUIRE(opened.value().execute("UPDATE schema_version SET version = 6")
                    .has_value());
    }

    auto reopened = database::open(scratch.path());
    REQUIRE(reopened.has_value());
    const auto applied = reopened.value().apply_schema();

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().code == store_errc::schema_mismatch);
    REQUIRE(applied.error().message.contains("6"));
    REQUIRE(applied.error().message.contains("delete the file"));

    /*
     * Refused, not half-migrated. A statement that names the icon key column is
     * refused because the column was never added, the shortcut table was not
     * created either, and the stamp still says 6. A read through the entity
     * would not prove this, because sqlgen tolerates a missing column and
     * returns rows without it, so the check is a write that has to name the
     * column.
     */
    REQUIRE_FALSE(
        reopened.value().execute("UPDATE instance SET icon_key = 'migrated'")
            .has_value());
    REQUIRE_FALSE(reopened.value().execute("UPDATE shortcut SET position = 0")
                      .has_value());
    const auto versions =
        sqlgen::read<std::vector<schema_version_entity>>(
            reopened.value().connection());
    REQUIRE(versions.has_value());
    REQUIRE(versions.value().size() == 1);
    REQUIRE(versions.value().front().version == 6);
}

TEST_CASE("every repository reports the DDL of its own table", "[store]") {
    auto db = fresh_store();
    instance_repository instances(db);
    account_repository accounts(db);
    folder_repository folders(db);
    label_repository labels(db);
    labelling_repository labellings(db);
    window_repository windows(db);
    master_repository master(db);
    shortcut_repository shortcuts(db);

    /*
     * The statement a repository reports is the statement the schema applies,
     * so this is also the check that a table's definition exists at all. A name
     * that no entity describes comes back empty rather than throwing, and the
     * failure below is what notices.
     */
    for (const auto& [name, ddl] : std::array<std::pair<std::string, std::string>, 8>{
             std::pair{std::string{"instance"}, instances.sql()},
             std::pair{std::string{"account"}, accounts.sql()},
             std::pair{std::string{"folder"}, folders.sql()},
             std::pair{std::string{"label"}, labels.sql()},
             std::pair{std::string{"instance_label"}, labellings.sql()},
             std::pair{std::string{"window_spec"}, windows.sql()},
             std::pair{std::string{"master"}, master.sql()},
             std::pair{std::string{"shortcut"}, shortcuts.sql()},
         }) {
        INFO("table " << name);
        REQUIRE_FALSE(ddl.empty());
        REQUIRE(ddl.contains("CREATE TABLE"));
        REQUIRE(ddl.contains(name));
    }

    /* The junctions are one repository, so both of its tables are reported. */
    REQUIRE(labellings.sql().contains("account_label"));
}

TEST_CASE("every entity reads back from the schema this build applies",
    "[store]") {
    using hedgr::store::repository::account_credential_entity;
    using hedgr::store::repository::account_entity;
    using hedgr::store::repository::account_label_entity;
    using hedgr::store::repository::folder_entity;
    using hedgr::store::repository::instance_entity;
    using hedgr::store::repository::instance_label_entity;
    using hedgr::store::repository::label_entity;
    using hedgr::store::repository::master_entity;
    using hedgr::store::repository::schema_version_entity;
    using hedgr::store::repository::shortcut_entity;
    using hedgr::store::repository::window_spec_entity;

    auto db = fresh_store();

    /*
     * The three tables that carry a cascade foreign key are written out by hand
     * rather than generated, so their columns could drift from the entities the
     * repositories bind. There is no view of a table's columns to compare
     * against, but there does not need to be: SQLite refuses a query that names
     * a column the table does not have, so a read through each entity is the
     * check, and it is the same read the repositories do.
     */
    const auto reads = std::array<std::pair<std::string_view, bool>, 11>{
        std::pair<std::string_view, bool>{"instance",
            sqlgen::read<std::vector<instance_entity>>(db.connection()).has_value()},
        std::pair<std::string_view, bool>{"account",
            sqlgen::read<std::vector<account_entity>>(db.connection()).has_value()},
        std::pair<std::string_view, bool>{"account credential",
            sqlgen::read<std::vector<account_credential_entity>>(db.connection())
                .has_value()},
        std::pair<std::string_view, bool>{"folder",
            sqlgen::read<std::vector<folder_entity>>(db.connection()).has_value()},
        std::pair<std::string_view, bool>{"label",
            sqlgen::read<std::vector<label_entity>>(db.connection()).has_value()},
        std::pair<std::string_view, bool>{"instance_label",
            sqlgen::read<std::vector<instance_label_entity>>(db.connection())
                .has_value()},
        std::pair<std::string_view, bool>{"account_label",
            sqlgen::read<std::vector<account_label_entity>>(db.connection())
                .has_value()},
        std::pair<std::string_view, bool>{"window_spec",
            sqlgen::read<std::vector<window_spec_entity>>(db.connection())
                .has_value()},
        std::pair<std::string_view, bool>{"shortcut",
            sqlgen::read<std::vector<shortcut_entity>>(db.connection())
                .has_value()},
        std::pair<std::string_view, bool>{"master",
            sqlgen::read<std::vector<master_entity>>(db.connection()).has_value()},
        std::pair<std::string_view, bool>{"schema_version",
            sqlgen::read<std::vector<schema_version_entity>>(db.connection())
                .has_value()},
    };
    for (const auto& [table, readable] : reads) {
        INFO("table " << table);
        REQUIRE(readable);
    }

    /*
     * And the two constraints sqlgen cannot state are facts in the file rather
     * than conventions in the code: the cascade that removes an instance's
     * accounts and the partial index that refuses a second root.
     */
    instance_repository instances(db);
    REQUIRE(instances.save(sample_instance()).has_value());

    account child;
    child.id = "id-1";
    child.instance_id = "inst-1";
    child.name = "ops read-only";
    child.principal = "ops@host";
    REQUIRE(account_repository(db).save(child).has_value());

    REQUIRE(instances.remove("inst-1").has_value());
    REQUIRE(account_repository(db).list().value().empty());
}
