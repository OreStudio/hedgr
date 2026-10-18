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
#include "hedgr.model/state/bucket_aggregator.hpp"
#include "hedgr.model/state/bucket_ring.hpp"
#include "hedgr.model/state/tenant_image_map.hpp"
#include <unordered_set>
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.model/state/estate_history.hpp"
#include "hedgr.model/state/probe_table.hpp"
#include "hedgr.model/state/market_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include <catch2/catch_all.hpp>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using hedgr::model::domain::account;
using hedgr::model::domain::bootstrap_status;
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
using hedgr::model::state::estate_history;
using hedgr::model::state::estate_sample;
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
    /*
     * A deployment that answered the bootstrap read, with a version it stated.
     * This is the populated case, so the table test can prove the structured
     * answer survives the round trip rather than only its empty default.
     */
    result.bootstrap = bootstrap_status{
        .in_bootstrap_mode = false,
        .has_tenant = true,
        .version = std::string{"capture build"}};
    return result;
}

/** @brief An estate sample carrying only what a ring test reads back. */
[[nodiscard]] estate_sample make_sample(std::int64_t observed_at_ns,
    std::int32_t ready, std::int32_t not_ready, std::int32_t unreachable,
    std::int32_t unknown, std::int32_t service_missing = 0) {
    estate_sample sample;
    sample.observed_at_ns = observed_at_ns;
    sample.ready = ready;
    sample.not_ready = not_ready;
    sample.service_missing = service_missing;
    sample.unreachable = unreachable;
    sample.unknown = unknown;
    return sample;
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
    sessions.set_account_result(handle, "acme", "books", "party-1",
        "secret-token");

    REQUIRE(sessions.tenant_name(handle) == "acme");
    REQUIRE(sessions.party_id(handle) == "party-1");
    sessions.remove(handle);

    REQUIRE_FALSE(sessions.contains(handle));
    REQUIRE(sessions.size() == 0);
}

TEST_CASE("the parties a login may act as are kept with its session", "[session]") {
    session_table sessions;
    const auto handle = sessions.insert("inst-1", "id-1");
    sessions.set_account_result(handle, "acme", "books", "party-1", "token");
    const std::string parties{"party-1\x1f"
        "Books\x1e"
        "party-2\x1f"
        "Trading"};
    sessions.set_parties(handle, parties);

    REQUIRE(sessions.parties(handle) == parties);

    sessions.remove(handle);
    REQUIRE_FALSE(sessions.contains(handle));
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

    /*
     * The structured answer comes back whole, field for field, which is what
     * the details pane draws its rows from. The detail sentence and the fields
     * travel together on an answered probe, because IAM's own remark is the
     * sentence and its state is the fields.
     */
    REQUIRE(probes.get(handle)->bootstrap.has_value());
    REQUIRE(probes.get(handle)->bootstrap->in_bootstrap_mode == false);
    REQUIRE(probes.get(handle)->bootstrap->has_tenant == true);
    REQUIRE(probes.get(handle)->bootstrap->version.has_value());
    REQUIRE(*probes.get(handle)->bootstrap->version == "capture build");

    /* An instance no probe has answered for has nothing to report. */
    const auto untouched = instances.upsert(make_instance("inst-2", "Other"));
    REQUIRE(probes.get(untouched) == nullptr);
    REQUIRE(probes.size() == 1);
}

TEST_CASE("a probe result without a bootstrap answer keeps its fields absent",
    "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto handle = instances.upsert(make_instance("inst-3", "Refused"));
    probe_result refused;
    refused.reach = reachability::unreachable;
    refused.ready = readiness::unknown;
    refused.detail = "connection refused";
    /*
     * No bootstrap answer at all. This is the free-text case: the sentence is
     * the whole report and the pane must not read the struct's defaults as a
     * deployment that answered false to everything.
     */
    probes.set(handle, refused);

    REQUIRE(probes.get(handle) != nullptr);
    REQUIRE_FALSE(probes.get(handle)->bootstrap.has_value());
    REQUIRE(probes.get(handle)->detail == "connection refused");
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
        {readiness::service_missing, "service missing"},
        {readiness::bootstrapping, "bootstrapping"},
        {readiness::ready, "ready"},
    };
    std::set<std::string_view> ready_names;
    for (const auto& [value, expected] : readies) {
        REQUIRE(hedgr::model::domain::to_string(value) == expected);
        ready_names.insert(hedgr::model::domain::to_string(value));
    }
    REQUIRE(ready_names.size() == 4);

    /*
     * Every enumerator, walked by value rather than by name, so a state added
     * without a spelling fails here instead of rendering as an empty cell. The
     * two axes are walked to their own counts, because readiness gained a state
     * that reachability never had.
     */
    for (std::uint8_t raw = 0; raw < 3; ++raw) {
        INFO("reachability " << static_cast<int>(raw));
        REQUIRE_FALSE(
            hedgr::model::domain::to_string(static_cast<reachability>(raw))
                .empty());
    }
    for (std::uint8_t raw = 0; raw < 4; ++raw) {
        INFO("readiness " << static_cast<int>(raw));
        REQUIRE_FALSE(
            hedgr::model::domain::to_string(static_cast<readiness>(raw))
                .empty());
    }
}

TEST_CASE("an estate window reads its polls back in the order they arrived",
    "[probe]") {
    estate_history history;
    REQUIRE(history.empty());

    history.append(make_sample(100, 1, 0, 0, 2));
    history.append(make_sample(200, 2, 0, 0, 1));
    history.append(make_sample(300, 0, 1, 0, 2));

    REQUIRE(history.size() == 3);
    REQUIRE(history.at(0).observed_at_ns == 100);
    REQUIRE(history.at(0).total() == 3);
    REQUIRE(history.at(1).observed_at_ns == 200);
    REQUIRE(history.at(1).ready == 2);
    REQUIRE(history.at(2).observed_at_ns == 300);

    /*
     * The array a plot walks is the same run the indexed reader sees, so the
     * chart never has to join two halves of a wrapped ring itself.
     */
    for (std::size_t index = 0; index < history.size(); ++index) {
        REQUIRE(history.data()[index].observed_at_ns
            == history.at(index).observed_at_ns);
    }
}

TEST_CASE("a full estate window drops the oldest poll and keeps the newest",
    "[probe]") {
    estate_history history;

    constexpr std::size_t overflow = 5;
    const auto appended = estate_history::capacity + overflow;
    for (std::size_t index = 0; index < appended; ++index) {
        history.append(make_sample(static_cast<std::int64_t>(index) * 10,
            1, 0, 0, 0));
    }

    /* The ring is full, so the first overflow samples have been overwritten. */
    REQUIRE(history.size() == estate_history::capacity);
    REQUIRE(history.at(0).observed_at_ns
        == static_cast<std::int64_t>(overflow) * 10);
    REQUIRE(history.at(history.size() - 1).observed_at_ns
        == static_cast<std::int64_t>(appended - 1) * 10);

    /* Every step forward in the run is a step forward in time. */
    for (std::size_t index = 1; index < history.size(); ++index) {
        REQUIRE(history.at(index).observed_at_ns
            > history.at(index - 1).observed_at_ns);
    }
}

TEST_CASE("a poll where nothing is known is still a sample", "[probe]") {
    estate_history history;
    history.append(make_sample(100, 0, 0, 0, 3));

    /*
     * An estate nobody has answered for is a poll of three unknown instances,
     * not an empty poll. Dropping it would leave a gap in the timeline where
     * the chart would draw nothing at all.
     */
    REQUIRE(history.size() == 1);
    REQUIRE(history.at(0).unknown == 3);
    REQUIRE(history.at(0).total() == 3);

    history.clear();
    REQUIRE(history.empty());
    REQUIRE(history.size() == 0);
}

TEST_CASE("a probe table records one estate poll per sweep", "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto ready = instances.upsert(make_instance("inst-ready", "Ready"));
    const auto silent = instances.upsert(make_instance("inst-silent", "Silent"));
    const auto down = instances.upsert(make_instance("inst-down", "Down"));
    static_cast<void>(instances.upsert(make_instance("inst-new", "New")));

    probes.set(ready, make_probe(reachability::reachable, readiness::ready));
    probes.set(silent,
        make_probe(reachability::reachable, readiness::service_missing));
    probes.set(down,
        make_probe(reachability::unreachable, readiness::unknown));

    probes.record_snapshot(1000, instances.size());

    REQUIRE(probes.estate().size() == 1);
    const auto& first = probes.estate().at(0);
    REQUIRE(first.ready == 1);
    REQUIRE(first.not_ready == 0);
    REQUIRE(first.service_missing == 1);
    REQUIRE(first.unreachable == 1);
    REQUIRE(first.unknown == 1);
    REQUIRE(first.total() == 4);

    /*
     * The estate shrinks, and the next poll's total follows the estate at that
     * poll rather than reusing the total the earlier bar was drawn with.
     */
    probes.forget(down);
    instances.remove(down);
    probes.record_snapshot(2000, instances.size());

    REQUIRE(probes.estate().size() == 2);
    const auto& second = probes.estate().at(1);
    REQUIRE(second.total() == 3);
    REQUIRE(second.unreachable == 0);
    REQUIRE(probes.estate().at(0).total() == 4);
}

TEST_CASE("four estate polls read back as four different buckets", "[probe]") {
    instance_table instances;
    probe_table probes;

    const auto a = instances.upsert(make_instance("a", "A"));
    const auto b = instances.upsert(make_instance("b", "B"));
    const auto c = instances.upsert(make_instance("c", "C"));

    /*
     * A poll of the whole estate, with the state moving one instance at a time.
     * A reader that showed the newest counts for every poll would pass on a
     * single sample and fail here, which is the point of four.
     */
    probes.record_snapshot(100, instances.size());

    probes.set(a, make_probe(reachability::reachable, readiness::ready));
    probes.record_snapshot(200, instances.size());

    probes.set(b, make_probe(reachability::unreachable, readiness::unknown));
    probes.record_snapshot(300, instances.size());

    probes.set(c,
        make_probe(reachability::reachable, readiness::bootstrapping));
    probes.record_snapshot(400, instances.size());

    const auto& history = probes.estate();
    REQUIRE(history.size() == 4);

    const std::array<std::int32_t, 4> ready{
        history.at(0).ready, history.at(1).ready,
        history.at(2).ready, history.at(3).ready};
    const std::array<std::int32_t, 4> not_ready{
        history.at(0).not_ready, history.at(1).not_ready,
        history.at(2).not_ready, history.at(3).not_ready};
    const std::array<std::int32_t, 4> unreachable{
        history.at(0).unreachable, history.at(1).unreachable,
        history.at(2).unreachable, history.at(3).unreachable};

    REQUIRE(ready == std::array<std::int32_t, 4>{0, 1, 1, 1});
    REQUIRE(not_ready == std::array<std::int32_t, 4>{0, 0, 0, 1});
    REQUIRE(unreachable == std::array<std::int32_t, 4>{0, 0, 1, 1});

    /* The four polls are four distinct minutes for the chart to place. */
    REQUIRE(history.at(0).observed_at_ns < history.at(1).observed_at_ns);
    REQUIRE(history.at(1).observed_at_ns < history.at(2).observed_at_ns);
    REQUIRE(history.at(2).observed_at_ns < history.at(3).observed_at_ns);
    for (std::size_t index = 1; index < history.size(); ++index)
        REQUIRE(history.at(index).observed_at_ns
            != history.at(index - 1).observed_at_ns);
}

TEST_CASE("the market table keeps a catalogue per session and points per chart",
    "[market]") {
    using hedgr::model::domain::market_point;
    using hedgr::model::domain::market_series_row;
    using hedgr::model::state::catalogue_state;
    using hedgr::model::state::market_table;

    market_table market;
    REQUIRE(market.catalogue("i-1", "a-1") == nullptr);

    market.begin_catalogue("i-1", "a-1");
    REQUIRE(market.catalogue("i-1", "a-1")->state == catalogue_state::reading);

    market.set_catalogue("i-1", "a-1", std::vector<market_series_row>{
        {.id = "s-1", .uri = "oresmd://fx/GBP?ccy=USD", .label = "FX GBP/USD",
            .asset_class = "FX", .producer_kind = "SYNTHETIC", .party_id = "p-1"}});
    market.set_catalogue("i-2", "a-2", std::vector<market_series_row>{
        {.id = "s-9", .uri = "oresmd://ir/GBP?tenor=1D", .label = "IR GBP 1D",
            .asset_class = "IR", .producer_kind = "SYNTHETIC", .party_id = "p-2"}});

    /*
     * Two instances, two accounts, two catalogues. One login's list is not
     * another's, and the pair is the key.
     */
    REQUIRE(market.catalogue("i-1", "a-1")->series.size() == 1);
    REQUIRE(market.catalogue("i-2", "a-2")->series.size() == 1);
    REQUIRE(market.catalogue("i-1", "a-2") == nullptr);

    REQUIRE(market.points("i-1", "a-1", "s-1") == nullptr);
    market.set_points("i-1", "a-1", "s-1",
        std::vector<market_point>{{.at_ns = 2'000'000'000, .value = 1.2},
            {.at_ns = 1'000'000'000, .value = 1.1}},
        10);
    const auto* points = market.points("i-1", "a-1", "s-1");
    REQUIRE(points != nullptr);
    REQUIRE(points->is_read());
    REQUIRE(points->fine.size() == 2);

    /* A failed read keeps the points and says the read failed. */
    market.fail_points("i-1", "a-1", "s-1", "no responder", 20);
    REQUIRE_FALSE(points->is_read());
    REQUIRE(points->fine.size() == 2);
    REQUIRE(points->failure == "no responder");

    market.clear();
    REQUIRE(market.catalogue("i-1", "a-1") == nullptr);
    REQUIRE(market.points("i-1", "a-1", "s-1") == nullptr);
}

TEST_CASE("a backfill fills all three levels and live points win",
    "[market]") {
    using hedgr::model::domain::market_level;
    using hedgr::model::domain::market_point;
    using hedgr::model::state::market_table;

    market_table market;
    market.set_points("i-1", "a-1", "s-1",
        std::vector<market_point>{
            {.at_ns = 1'000'000'000, .value = 1.0},
            {.at_ns = 1'500'000'000, .value = 2.0},
            {.at_ns = 2'000'000'000, .value = 3.0},
            {.at_ns = 61'000'000'000, .value = 4.0}},
        10);

    const auto* snapshot = market.points("i-1", "a-1", "s-1");
    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->fine.size() == 3);
    REQUIRE(snapshot->fine.front().open == 1.0);
    REQUIRE(snapshot->fine.front().high == 2.0);
    REQUIRE(snapshot->fine[1].high == 3.0);
    REQUIRE(snapshot->fine.back().value == 4.0);
    REQUIRE(snapshot->medium.size() == 2);
    REQUIRE(snapshot->coarse.size() == 1);

    market.append_point("i-1", "a-1", "s-1", market_level::fine,
        market_point{.at_ns = 62'000'000'000, .value = 5.0}, 20);
    REQUIRE(snapshot->fine.back().value == 5.0);

    /* A point older than the newest bucket is dropped, not rewritten. */
    market.append_point("i-1", "a-1", "s-1", market_level::fine,
        market_point{.at_ns = 500'000'000, .value = 9.0}, 21);
    REQUIRE(snapshot->fine.back().value == 5.0);

    /* The backfill saw 4.0 where the subscription already has 5.0. */
    market.set_points("i-1", "a-1", "s-1",
        std::vector<market_point>{
            {.at_ns = 62'000'000'000, .value = 4.0}},
        22);
    REQUIRE(snapshot->fine.back().value == 5.0);
}

TEST_CASE("the bucket ring keeps order and drops when full", "[ring]") {
    using hedgr::model::domain::market_level;
    using hedgr::model::domain::market_point;
    using hedgr::model::state::bucket_ring;
    using hedgr::model::state::market_bucket;

    bucket_ring ring;
    std::vector<market_bucket> out;

    for (int i = 0; i < 10; ++i) {
        REQUIRE(ring.try_push(market_bucket{.series_id = "s-1",
            .level = market_level::fine,
            .point = market_point{.at_ns = i, .value = static_cast<double>(i)}}));
    }
    REQUIRE(ring.drain(out) == 10);
    REQUIRE(out.size() == 10);
    REQUIRE(out.front().point.at_ns == 0);
    REQUIRE(out.back().point.at_ns == 9);
    REQUIRE(ring.high_water() == 10);

    for (std::size_t i = 0; i < bucket_ring::capacity + 5; ++i) {
        ring.try_push(market_bucket{.series_id = "s-2",
            .level = market_level::medium,
            .point = market_point{.at_ns = static_cast<std::int64_t>(i),
                .value = 1.0}});
    }
    REQUIRE(ring.dropped() == 5);
    REQUIRE(ring.drain(out) == bucket_ring::capacity);
    REQUIRE(out.back().point.at_ns
        == static_cast<std::int64_t>(bucket_ring::capacity - 1));

    /* The ring wraps: the indices are taken modulo the capacity. */
    for (std::int64_t i = 0; i < 3; ++i) {
        ring.try_push(market_bucket{.series_id = "s-3",
            .level = market_level::coarse,
            .point = market_point{.at_ns = i, .value = 2.0}});
    }
    REQUIRE(ring.drain(out) == 3);
    REQUIRE(out.front().point.at_ns == 0);
    REQUIRE(out.back().point.at_ns == 2);
}

TEST_CASE("ticks close their own bucket at each resolution", "[aggregator]") {
    using hedgr::model::domain::market_level;
    using hedgr::model::state::bucket_aggregator;
    using hedgr::model::state::bucket_ring;
    using hedgr::model::state::market_bucket;

    bucket_ring ring;
    bucket_aggregator aggregator{"s-1", ring};
    std::vector<market_bucket> out;

    aggregator.on_tick(200'000'000, 1.0);
    aggregator.on_tick(800'000'000, 2.0);
    REQUIRE(ring.drain(out) == 0);

    aggregator.on_tick(1'200'000'000, 3.0);
    REQUIRE(ring.drain(out) == 1);
    REQUIRE(out.front().level == market_level::fine);
    REQUIRE(out.front().point.at_ns == 800'000'000);
    REQUIRE(out.front().point.value == 2.0);
    REQUIRE(out.front().point.open == 1.0);
    REQUIRE(out.front().point.high == 2.0);
    REQUIRE(out.front().point.low == 1.0);

    aggregator.on_tick(61'200'000'000, 4.0);
    REQUIRE(ring.drain(out) == 2);
    REQUIRE(out[0].level == market_level::fine);
    REQUIRE(out[0].point.value == 3.0);
    REQUIRE(out[1].level == market_level::medium);
    REQUIRE(out[1].point.at_ns == 1'200'000'000);
    REQUIRE(out[1].point.open == 1.0);
    REQUIRE(out[1].point.high == 3.0);
    REQUIRE(out[1].point.low == 1.0);
    REQUIRE(out[1].point.value == 3.0);

    /* A late tick is dropped by every level, not written out of order. */
    aggregator.on_tick(500'000'000, 9.0);
    REQUIRE(ring.drain(out) == 0);

    aggregator.on_tick(122'400'000'000, 5.0);
    REQUIRE(ring.drain(out) == 2);
    REQUIRE(out[0].level == market_level::fine);
    REQUIRE(out[0].point.value == 4.0);
    REQUIRE(out[1].level == market_level::medium);
    REQUIRE(out[1].point.value == 4.0);
    REQUIRE(aggregator.ticks() == 6);
}

TEST_CASE("a tenant image map answers by currency", "[images]") {
    using hedgr::model::state::image_map_state;
    using hedgr::model::state::tenant_image_map;

    tenant_image_map images;
    REQUIRE(images.image_for("t-1", "GBP") == nullptr);
    images.begin("t-1");
    REQUIRE(images.get("t-1")->state == image_map_state::reading);

    images.set("t-1", {{"GBP", "id-gb"}, {"USD", "id-us"}});
    REQUIRE(images.image_for("t-1", "GBP") != nullptr);
    REQUIRE(*images.image_for("t-1", "GBP") == "id-gb");
    REQUIRE(images.image_for("t-2", "GBP") == nullptr);

    images.fail("t-2", "no currencies");
    REQUIRE(images.get("t-2")->state == image_map_state::failed);
    REQUIRE(images.get("t-2")->error == "no currencies");
}

TEST_CASE("the image cache keeps tenants apart and evicts least recently used",
    "[images]") {
    using hedgr::model::state::image_cache;
    using hedgr::model::state::image_content;

    image_cache cache{12};
    const auto make = [](std::uint8_t v) {
        return image_content{.mime_type = "image/png",
            .data = std::vector<std::uint8_t>(4, v)};
    };

    cache.put("t-1", "i-1", make(1));
    cache.put("t-1", "i-2", make(2));
    cache.put("t-2", "i-1", make(3));
    REQUIRE(cache.bytes() == 12);
    REQUIRE(cache.get("t-1", "i-1") != nullptr);
    REQUIRE(cache.get("t-2", "i-1")->data[0] == 3);
    REQUIRE(cache.get("t-1", "i-2")->data[0] == 2);

    /* i-1 is now the least recently used, so a fourth image evicts it. */
    cache.put("t-1", "i-3", make(4));
    REQUIRE_FALSE(cache.contains("t-1", "i-1"));
    REQUIRE(cache.get("t-1", "i-2")->data[0] == 2);
    REQUIRE(cache.evictions() == 1);

    /* An image larger than the whole budget is not cached. */
    image_content big{.mime_type = "image/png",
        .data = std::vector<std::uint8_t>(13)};
    cache.put("t-1", "big", std::move(big));
    REQUIRE_FALSE(cache.contains("t-1", "big"));
    REQUIRE(cache.get("t-1", "missing") == nullptr);
    REQUIRE(cache.misses() >= 1);
}

TEST_CASE("a restored window keeps the session it was saved against",
    "[window]") {
    using hedgr::model::domain::chart_state;
    using hedgr::model::domain::session_key;
    using hedgr::model::domain::window_kind;
    using hedgr::model::domain::window_spec;
    using hedgr::model::state::session_table;
    using hedgr::model::state::window_table;

    window_table windows;
    session_table sessions;

    window_spec spec;
    spec.id = "chart:inst-1:acct-1:s-1";
    spec.kind = window_kind::chart;
    spec.session = session_key{"inst-1", "acct-1"};
    spec.state = chart_state{.label = "FX GBP/USD", .series_id = "s-1"};
    const auto handle = windows.insert(spec);

    /*
     * A window wall is restored before any session exists, so the row is
     * unbound. The pair it was saved against is still reported, which is what
     * lets the sign-in that owns it bind the window later.
     */
    REQUIRE_FALSE(windows.session_of(handle).has_value());
    const auto stored = windows.spec(handle, sessions);
    REQUIRE(stored.session.has_value());
    REQUIRE(stored.session->instance_id == "inst-1");
    REQUIRE(stored.session->account_id == "acct-1");
}
