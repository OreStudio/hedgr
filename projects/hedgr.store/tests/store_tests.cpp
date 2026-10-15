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
#include "hedgr.model/domain/identity.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.store/repository/identity_repository.hpp"
#include "hedgr.store/repository/instance_repository.hpp"
#include "hedgr.store/repository/schema.hpp"
#include "hedgr.store/repository/window_repository.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

namespace {

using hedgr::model::domain::chart_state;
using hedgr::model::domain::identity;
using hedgr::model::domain::instance;
using hedgr::model::domain::session_key;
using hedgr::model::domain::tier;
using hedgr::model::domain::window_kind;
using hedgr::model::domain::window_spec;
using hedgr::store::repository::identity_repository;
using hedgr::store::repository::instance_repository;
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

instance sample_instance() {
    instance row;
    row.id = "inst-1";
    row.name = "Local";
    row.tier = tier::production;
    row.nats_url = "nats://localhost:20405";
    row.subject_prefix = "ores.local";
    row.wire_format = "msgpack";
    row.labels = "dev,local";
    return row;
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
    REQUIRE(row.tier == tier::production);
    REQUIRE(row.nats_url == "nats://localhost:20405");
    REQUIRE(row.subject_prefix == "ores.local");
    REQUIRE(row.wire_format == "msgpack");
    REQUIRE(row.labels == "dev,local");
}

TEST_CASE("saving the same id twice updates the row", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    REQUIRE(repository.save(sample_instance()).has_value());

    instance updated = sample_instance();
    updated.name = "Local Renamed";
    updated.tier = tier::staging;
    REQUIRE(repository.save(updated).has_value());

    const auto rows = repository.list();
    REQUIRE(rows.has_value());
    REQUIRE(rows.value().size() == 1);
    REQUIRE(rows.value().front().name == "Local Renamed");
    REQUIRE(rows.value().front().tier == tier::staging);
}

TEST_CASE("removing an instance cascades to its identities", "[store]") {
    auto db = fresh_store();
    instance_repository instances(db);
    identity_repository identities(db);

    REQUIRE(instances.save(sample_instance()).has_value());

    identity row;
    row.id = "id-1";
    row.instance_id = "inst-1";
    row.label = "ops read-only";
    row.principal = "ops@host";
    REQUIRE(identities.save(row).has_value());
    REQUIRE(identities.list_for_instance("inst-1").value().size() == 1);

    REQUIRE(instances.remove("inst-1").has_value());

    const auto remaining = identities.list_for_instance("inst-1");
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
    REQUIRE(stored.session->identity_id == "id-1");
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

TEST_CASE("an unknown tier in a row is reported as a corrupt row", "[store]") {
    auto db = fresh_store();
    instance_repository repository(db);

    const auto planted = db.execute(
        "INSERT INTO instance (id, name, tier, nats_url, subject_prefix) "
        "VALUES ('inst-bad', 'Bad', 'quantum', 'nats://localhost:1', "
        "'ores.bad')");
    REQUIRE(planted.has_value());

    const auto rows = repository.list();
    REQUIRE_FALSE(rows.has_value());
    REQUIRE(rows.error().code == store_errc::corrupt_row);
    REQUIRE(rows.error().message.contains("inst-bad"));
}
