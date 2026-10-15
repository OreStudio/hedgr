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
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using hedgr::model::domain::account;
using hedgr::model::domain::combine;
using hedgr::model::domain::folder;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::label;
using hedgr::model::domain::label_severity;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::probe_result;
using hedgr::model::domain::reachability;
using hedgr::model::domain::readiness;
using hedgr::model::domain::session_status;
using hedgr::model::domain::window_kind;
using hedgr::model::domain::window_spec;
using hedgr::model::state::account_table;
using hedgr::model::state::folder_table;
using hedgr::model::state::instance_table;
using hedgr::model::state::label_table;
using hedgr::model::state::labelling_table;
using hedgr::model::state::probe_table;
using hedgr::model::state::session_table;
using hedgr::model::state::window_table;

/** @brief An instance row carrying only what a table test reads back. */
[[nodiscard]] instance make_instance(std::string id, std::string name) {
    instance row;
    row.id = std::move(id);
    row.name = std::move(name);
    return row;
}

/** @brief An account row belonging to @p instance_id. */
[[nodiscard]] account make_account(std::string id, std::string instance_id) {
    account row;
    row.id = std::move(id);
    row.instance_id = std::move(instance_id);
    return row;
}

/** @brief A folder row with its parent and its place in a tree named. */
[[nodiscard]] folder make_folder(std::string id, std::string parent_id,
    folder_tree tree, std::string owner_id = {}) {
    folder row;
    row.id = std::move(id);
    row.parent_id = std::move(parent_id);
    row.owner_id = std::move(owner_id);
    row.name = row.id;
    row.tree = tree;
    return row;
}

/** @brief A label row carrying only what a table test reads back. */
[[nodiscard]] label make_label(std::string id, std::string name) {
    label row;
    row.id = std::move(id);
    row.name = std::move(name);
    return row;
}

/** @brief A probe result carrying only what a table test reads back. */
[[nodiscard]] probe_result make_probe(reachability reach, readiness ready) {
    probe_result result;
    result.reach = reach;
    result.ready = ready;
    result.latency_us = 4321;
    result.observed_at_ns = 99;
    result.detail = "no service is handling subject 'iam.v1.ops.bootstrap_status'";
    return result;
}

TEST_CASE("an instance is found by id and read back by handle", "[instance]") {
    instance_table instances;

    const auto handle = instances.upsert(make_instance("inst-1", "Local"));

    REQUIRE(instances.size() == 1);
    REQUIRE(instances.contains(handle));
    REQUIRE(instances.find("inst-1") == handle);
    REQUIRE(instances.get(handle) != nullptr);
    REQUIRE(instances.get(handle)->name == "Local");
}

TEST_CASE("a replaced instance keeps the handle it held", "[instance]") {
    instance_table instances;
    const auto first = instances.upsert(make_instance("inst-1", "Local"));

    const auto second = instances.upsert(make_instance("inst-1", "Renamed"));

    REQUIRE(second == first);
    REQUIRE(instances.size() == 1);
    REQUIRE(instances.get(first)->name == "Renamed");
}

TEST_CASE("a removed instance leaves a hole no handle reaches", "[instance]") {
    instance_table instances;
    const auto first = instances.upsert(make_instance("inst-1", "Local"));

    instances.remove(first);

    REQUIRE(instances.size() == 0);
    REQUIRE_FALSE(instances.contains(first));
    REQUIRE(instances.get(first) == nullptr);
    REQUIRE_FALSE(instances.find("inst-1").has_value());

    /* The slot stays where it was, which keeps every other handle valid. */
    REQUIRE(instances.rows().size() == 1);
    REQUIRE_FALSE(instances.layout().is_alive(0));

    const auto second = instances.upsert(make_instance("inst-2", "Next"));
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);
    REQUIRE_FALSE(second == first);
}

TEST_CASE("an account table reports the children of an instance", "[account]") {
    account_table accounts;
    const auto first = accounts.upsert(make_account("acc-1", "inst-1"));
    static_cast<void>(accounts.upsert(make_account("acc-2", "inst-2")));
    static_cast<void>(accounts.upsert(make_account("acc-3", "inst-1")));

    REQUIRE(accounts.children_of("inst-1").size() == 2);
    REQUIRE(accounts.children_of("inst-2").size() == 1);

    accounts.remove(first);

    const auto children = accounts.children_of("inst-1");
    REQUIRE(children.size() == 1);
    REQUIRE(children.front()->id == "acc-3");
}

TEST_CASE("a session is found by the instance and account pair", "[session]") {
    session_table sessions;
    const auto handle = sessions.insert("inst-1", "id-1");

    REQUIRE(sessions.size() == 1);
    REQUIRE(sessions.status(handle) == session_status::disconnected);
    REQUIRE(sessions.find("inst-1", "id-1") == handle);
    REQUIRE_FALSE(sessions.find("inst-1", "id-2").has_value());
}

TEST_CASE("a released handle does not address the next occupant", "[session]") {
    session_table sessions;
    const auto first = sessions.insert("inst-1", "id-1");
    sessions.remove(first);

    REQUIRE(sessions.size() == 0);
    REQUIRE_FALSE(sessions.contains(first));

    const auto second = sessions.insert("inst-1", "id-1");
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);
    REQUIRE_FALSE(second == first);
    REQUIRE(sessions.contains(second));
}

TEST_CASE("removing a session drops the token it held", "[session]") {
    session_table sessions;
    const auto handle = sessions.insert("inst-1", "id-1");
    sessions.set_account_result(handle, "acme", "books", "secret-token");

    REQUIRE(sessions.tenant_name(handle) == "acme");
    sessions.remove(handle);

    REQUIRE_FALSE(sessions.contains(handle));
    REQUIRE(sessions.size() == 0);
}

TEST_CASE("the status column stays stride-1 over holes", "[session]") {
    session_table sessions;
    const auto first = sessions.insert("inst-1", "id-1");
    const auto second = sessions.insert("inst-1", "id-2");
    sessions.set_status(second, session_status::authenticated);
    sessions.remove(first);

    const auto column = sessions.status_column();
    REQUIRE(column.size() == 2);
    REQUIRE(column[second.index] ==
            static_cast<std::uint8_t>(session_status::authenticated));
}

TEST_CASE("a window keeps its stable id across a reopen", "[window]") {
    window_table windows;
    const auto spec = window_spec{
        .id = "w-trades",
        .kind = window_kind::grid,
    };
    const auto handle = windows.insert(spec);

    REQUIRE(windows.size() == 1);
    REQUIRE(windows.is_open(handle));
    REQUIRE(windows.find("w-trades") == handle);
    REQUIRE(windows.kind(handle) == window_kind::grid);

    windows.set_open(handle, false);
    REQUIRE_FALSE(windows.is_open(handle));

    windows.set_open(handle, true);
    REQUIRE(windows.find("w-trades") == handle);
}

TEST_CASE("a window description round-trips through the table", "[window]") {
    session_table sessions;
    window_table windows;
    const auto session = sessions.insert("inst-1", "id-1");

    window_spec spec;
    spec.id = "w-chart";
    spec.kind = window_kind::chart;
    spec.session = hedgr::model::domain::session_key{"inst-1", "id-1"};
    const auto handle = windows.insert(spec);
    windows.bind_session(handle, session);

    REQUIRE(windows.session_of(handle) == session);

    const auto recovered = windows.spec(handle, sessions);
    REQUIRE(recovered.id == "w-chart");
    REQUIRE(recovered.kind == window_kind::chart);
    REQUIRE(recovered.session.has_value());
    REQUIRE(recovered.session->instance_id == "inst-1");
    REQUIRE(recovered.session->account_id == "id-1");

    REQUIRE(windows.specs(sessions).size() == 1);
}

TEST_CASE("a folder table reports only the direct children of a folder",
    "[folder]") {
    folder_table folders;
    static_cast<void>(folders.upsert(
        make_folder("root", "", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("child", "root", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("grandchild", "child", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("sibling", "root", folder_tree::instances)));

    const auto children = folders.children_of("root");
    REQUIRE(children.size() == 2);
    REQUIRE(children[0]->id == "child");
    REQUIRE(children[1]->id == "sibling");

    /* The grandchild is below the child, not beside it. */
    const auto child_children = folders.children_of("child");
    REQUIRE(child_children.size() == 1);
    REQUIRE(child_children[0]->id == "grandchild");

    /* The children are the folder's own, not the whole tree. */
    REQUIRE(folders.children_of("grandchild").empty());
}

TEST_CASE("a folder table resolves the root of each tree by owner",
    "[folder]") {
    folder_table folders;
    static_cast<void>(folders.upsert(
        make_folder("instances-root", "", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("accounts-root:inst-1", "", folder_tree::accounts,
            "inst-1")));
    static_cast<void>(folders.upsert(
        make_folder("accounts-root:inst-2", "", folder_tree::accounts,
            "inst-2")));
    static_cast<void>(folders.upsert(
        make_folder("child", "accounts-root:inst-1", folder_tree::accounts,
            "inst-1")));

    const auto instances = folders.root(folder_tree::instances, "");
    REQUIRE(instances.has_value());
    REQUIRE(folders.get(*instances)->id == "instances-root");

    const auto first = folders.root(folder_tree::accounts, "inst-1");
    REQUIRE(first.has_value());
    REQUIRE(folders.get(*first)->id == "accounts-root:inst-1");

    const auto second = folders.root(folder_tree::accounts, "inst-2");
    REQUIRE(second.has_value());
    REQUIRE(folders.get(*second)->id == "accounts-root:inst-2");

    /* An owner with no accounts tree answers with nothing, not with a guess. */
    REQUIRE_FALSE(folders.root(folder_tree::accounts, "inst-3").has_value());
}

TEST_CASE("a folder cannot be made its own ancestor", "[folder]") {
    folder_table folders;
    static_cast<void>(folders.upsert(
        make_folder("root", "", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("child", "root", folder_tree::instances)));
    static_cast<void>(folders.upsert(
        make_folder("grandchild", "child", folder_tree::instances)));

    /* Root parented to its own grandchild would close the loop. */
    const auto cyclic = folders.upsert(make_folder("root", "grandchild",
        folder_tree::instances));
    REQUIRE_FALSE(cyclic.has_value());
    REQUIRE(cyclic.error() ==
        "cannot parent folder root to grandchild: it would contain itself");

    /* The refusal leaves the tree exactly as it was. */
    REQUIRE(folders.get(*folders.find("root"))->parent_id == "");

    /* A folder is likewise not its own parent. */
    const auto self = folders.upsert(make_folder("child", "child",
        folder_tree::instances));
    REQUIRE_FALSE(self.has_value());

    /* The descendants are what the containment check walks. */
    const auto descendants = folders.descendants_of("root");
    REQUIRE(descendants.size() == 2);
    const auto ancestors = folders.ancestors_of("grandchild");
    REQUIRE(ancestors.size() == 2);
    REQUIRE(ancestors[0] == "child");
    REQUIRE(ancestors[1] == "root");
}

TEST_CASE("combining two severities keeps the higher one", "[label]") {
    REQUIRE(combine(label_severity::none, label_severity::none)
        == label_severity::none);
    REQUIRE(combine(label_severity::none, label_severity::caution)
        == label_severity::caution);
    REQUIRE(combine(label_severity::caution, label_severity::none)
        == label_severity::caution);
    REQUIRE(combine(label_severity::caution, label_severity::danger)
        == label_severity::danger);
    REQUIRE(combine(label_severity::danger, label_severity::caution)
        == label_severity::danger);

    /* The fold over a label set does not depend on the order it is walked. */
    auto severity = label_severity::none;
    for (const auto value : {label_severity::caution, label_severity::danger,
             label_severity::none}) {
        severity = combine(severity, value);
    }
    REQUIRE(severity == label_severity::danger);
}

TEST_CASE("attaching a label twice is the same as attaching it once",
    "[labelling]") {
    labelling_table labellings;

    labellings.add(labelling_owner::instance, "inst-1", "lbl-prod");
    labellings.add(labelling_owner::instance, "inst-1", "lbl-prod");

    REQUIRE(labellings.size() == 1);

    labellings.add(labelling_owner::instance, "inst-1", "lbl-ops");
    labellings.add(labelling_owner::instance, "inst-2", "lbl-prod");
    REQUIRE(labellings.size() == 3);
}

TEST_CASE("a label list is scoped to its owner", "[labelling]") {
    labelling_table labellings;
    labellings.add(labelling_owner::instance, "inst-1", "lbl-prod");
    labellings.add(labelling_owner::instance, "inst-1", "lbl-ops");

    /*
     * The account carries the same id as the instance on purpose: the kind is
     * part of the identity, so the two owners must not share a label list.
     */
    labellings.add(labelling_owner::account, "inst-1", "lbl-staging");

    const auto instance_labels =
        labellings.labels_of(labelling_owner::instance, "inst-1");
    REQUIRE(instance_labels.size() == 2);
    REQUIRE(instance_labels[0] == "lbl-prod");
    REQUIRE(instance_labels[1] == "lbl-ops");

    const auto account_labels =
        labellings.labels_of(labelling_owner::account, "inst-1");
    REQUIRE(account_labels.size() == 1);
    REQUIRE(account_labels[0] == "lbl-staging");

    labellings.remove(labelling_owner::instance, "inst-1", "lbl-prod");
    REQUIRE(labellings.labels_of(labelling_owner::instance, "inst-1").size()
        == 1);

    labellings.remove_owner(labelling_owner::instance, "inst-1");
    REQUIRE(labellings.labels_of(labelling_owner::instance, "inst-1").empty());
    REQUIRE(labellings.size() == 1);
}

TEST_CASE("a label is found by its name as well as its id", "[label]") {
    label_table labels;
    label p = make_label("lbl-prod", "production");
    p.severity = label_severity::danger;
    const auto handle = labels.upsert(p);
    static_cast<void>(labels.upsert(make_label("lbl-dev", "development")));

    REQUIRE(labels.size() == 2);
    REQUIRE(labels.find("lbl-prod") == handle);
    REQUIRE(labels.find_by_name("production") == handle);
    REQUIRE(labels.get(handle)->severity == label_severity::danger);
    REQUIRE_FALSE(labels.find_by_name("staging").has_value());

    labels.remove(handle);
    REQUIRE_FALSE(labels.find_by_name("production").has_value());
    REQUIRE(labels.find_by_name("development").has_value());
}

TEST_CASE("a probe result is read back by the handle it was set with",
    "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto handle = instances.upsert(make_instance("inst-1", "Local"));
    probes.set(handle, make_probe(reachability::reachable, readiness::ready));

    REQUIRE(probes.size() == 1);
    REQUIRE(probes.get(handle) != nullptr);
    REQUIRE(probes.get(handle)->reach == reachability::reachable);
    REQUIRE(probes.get(handle)->ready == readiness::ready);
    REQUIRE(probes.get(handle)->latency_us == 4321);
    REQUIRE(probes.get(handle)->observed_at_ns == 99);
    REQUIRE(probes.get(handle)->detail ==
        "no service is handling subject 'iam.v1.ops.bootstrap_status'");

    /* An instance no probe has answered for has nothing to report. */
    const auto untouched = instances.upsert(make_instance("inst-2", "Other"));
    REQUIRE(probes.get(untouched) == nullptr);
    REQUIRE(probes.size() == 1);
}

TEST_CASE("a probe result does not follow a handle into the next occupant",
    "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto first = instances.upsert(make_instance("inst-1", "Local"));
    probes.set(first, make_probe(reachability::unreachable, readiness::unknown));

    instances.remove(first);
    const auto second = instances.upsert(make_instance("inst-2", "Other"));

    /* The slot is reused, so only the generation tells the two rows apart. */
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);

    /*
     * The row the probe wrote is not handed to the instance that took the
     * slot: the generations differ, so the new handle addresses nothing.
     */
    REQUIRE(probes.get(second) == nullptr);

    /*
     * Dropping the row is the runtime's job, done when it removes an instance:
     * the table cannot see the instance table and so cannot know that a
     * removal happened at all.
     */
    probes.forget(first);
    REQUIRE(probes.get(first) == nullptr);
    REQUIRE(probes.size() == 0);
}

TEST_CASE("forgetting an instance drops its probe row and its deadline",
    "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto handle = instances.upsert(make_instance("inst-1", "Local"));
    probes.set(handle, make_probe(reachability::reachable, readiness::ready));
    probes.set_next_probe_ns(handle, 42);

    REQUIRE(probes.size() == 1);
    REQUIRE(probes.next_probe_ns(handle) == 42);

    probes.forget(handle);

    REQUIRE(probes.size() == 0);
    REQUIRE(probes.get(handle) == nullptr);
    REQUIRE(probes.next_probe_ns(handle) == 0);

    probes.clear();
    REQUIRE(probes.size() == 0);
}

TEST_CASE("a probe deadline is kept per instance", "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto first = instances.upsert(make_instance("inst-1", "Local"));
    const auto second = instances.upsert(make_instance("inst-2", "Other"));

    /* An instance the schedule has not been told about is due now. */
    REQUIRE(probes.next_probe_ns(first) == 0);

    probes.set_next_probe_ns(first, 900);
    probes.set_next_probe_ns(second, 100);

    REQUIRE(probes.next_probe_ns(first) == 900);
    REQUIRE(probes.next_probe_ns(second) == 100);
    REQUIRE(probes.size() == 2);

    /* Setting a result leaves the deadline beside it alone. */
    probes.set(first, make_probe(reachability::reachable, readiness::ready));
    REQUIRE(probes.next_probe_ns(first) == 900);
}

TEST_CASE("every reachability and readiness has a distinct stable name",
    "[probe]") {
    const std::pair<reachability, std::string_view> reaches[] = {
        {reachability::unknown, "unknown"},
        {reachability::unreachable, "unreachable"},
        {reachability::reachable, "reachable"},
    };
    std::set<std::string_view> reach_names;
    for (const auto& [value, expected] : reaches) {
        REQUIRE(hedgr::model::domain::to_string(value) == expected);
        reach_names.insert(hedgr::model::domain::to_string(value));
    }
    REQUIRE(reach_names.size() == 3);

    const std::pair<readiness, std::string_view> readies[] = {
        {readiness::unknown, "unknown"},
        {readiness::bootstrapping, "bootstrapping"},
        {readiness::ready, "ready"},
    };
    std::set<std::string_view> ready_names;
    for (const auto& [value, expected] : readies) {
        REQUIRE(hedgr::model::domain::to_string(value) == expected);
        ready_names.insert(hedgr::model::domain::to_string(value));
    }
    REQUIRE(ready_names.size() == 3);

    /*
     * Every enumerator, walked by value rather than by name, so a fourth
     * state added without a spelling fails here instead of rendering as an
     * empty cell.
     */
    for (std::uint8_t raw = 0; raw < 3; ++raw) {
        INFO("reachability " << static_cast<int>(raw));
        REQUIRE_FALSE(
            hedgr::model::domain::to_string(static_cast<reachability>(raw))
                .empty());
        REQUIRE_FALSE(
            hedgr::model::domain::to_string(static_cast<readiness>(raw))
                .empty());
    }
}
