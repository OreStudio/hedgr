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
#include "hedgr.runtime/service/read_service.hpp"
#include "hedgr.runtime/worker/curve_worker.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

/*
 * The check the window cannot make for itself: Hedgr's own read path, over a
 * real deployment, for the curve a person clicked.
 *
 * Every wrong answer this window has given - "1D", a single point, an empty
 * plot - was a claim about this path that nothing tested end to end. A probe of
 * the protocol proves the deployment answers; this proves the code that the
 * window runs turns that answer into a ladder.
 *
 * Hidden, with the live environment:
 *
 *     HEDGR_LIVE_URL=nats://localhost:20405 \
 *     HEDGR_LIVE_PREFIX=ores.dev.brave_hopper \
 *     HEDGR_LIVE_PRINCIPAL=super_admin HEDGR_LIVE_PASSWORD=... \
 *     HEDGR_LIVE_CA=<checkout>/build/keys/nats/ca.crt \
 *     HEDGR_LIVE_CERT=<checkout>/build/keys/nats/ores.shell.crt \
 *     HEDGR_LIVE_KEY=<checkout>/build/keys/nats/ores.shell.key \
 *     hedgr.runtime.tests "[.live-curve]"
 *
 * `HEDGR_LIVE_CURVE` names the curve by its label, for example
 * `IR AUD IR_SWAP`; without it the first composite IR series is used.
 */
TEST_CASE("live curve: the window's own read of one curve", "[.live-curve]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    namespace transport = hedgr::session::transport;
    transport::session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    transport::session_client session(options);
    REQUIRE(session.connect());
    const auto signed_in = session.authenticate(principal, password);
    REQUIRE(signed_in.has_value());

    const auto catalogue = session.market_series(500);
    REQUIRE(catalogue.has_value());
    const auto* wanted = std::getenv("HEDGR_LIVE_CURVE");
    const transport::market_series_summary* chosen = nullptr;
    std::printf("catalogue: %zu rows\n", catalogue->size());
    for (const auto& row : *catalogue) {
        if (row.asset_class != "IR")
            continue;
        if (wanted != nullptr && row.label != wanted) {
            std::printf("  IR '%s' tenor='%s' curve='%s'\n", row.label.c_str(),
                row.tenor.c_str(), row.curve_key.c_str());
            continue;
        }
        if (wanted == nullptr && !row.tenor.empty())
            continue;
        std::printf("curve '%s' id=%s provider='%s' identity='%s' uri=%s\n",
            row.label.c_str(), row.id.c_str(), row.provider_code.c_str(),
            row.identity.empty() ? "(none)" : row.identity.instrument_type.c_str(),
            row.oresmd_uri.c_str());
        chosen = &row;
        break;
    }
    REQUIRE(chosen != nullptr);
    if (chosen == nullptr)
        return;

    /*
     * The request the window builds: the clicked series, no tenor of its own,
     * and the identity the catalogue parsed for it.
     */
    hedgr::runtime::worker::curve_request request{.key = chosen->curve_key.empty()
            ? chosen->id
            : chosen->curve_key,
        .label = chosen->label,
        .published = false,
        .member_ids = {chosen->id},
        .member_tenors = {std::string{}},
        .member_identity = chosen->identity};

    transport::session_client_link link(session);
    hedgr::model::event::event answer;
    answer.curve_key = request.key;
    hedgr::runtime::worker::run_curve_read(link, request, answer);
    std::printf("read: %zu pillars, %zu instants, message '%s'\n",
        answer.curve_pillars.size(), answer.curve_as_of_ns.size(),
        answer.message.c_str());
    for (std::size_t i = 0; i < answer.curve_pillars.size(); ++i)
        std::printf("  %s", answer.curve_pillars[i].c_str());
    std::printf("\n");
    std::fflush(stdout);

    /* The window draws one point when this is short, and that is the bug. */
    REQUIRE(answer.message.empty());
    REQUIRE(answer.curve_pillars.size() >= 2);
    REQUIRE(answer.curve_pillars.size() == answer.curve_values.size()
        / answer.curve_as_of_ns.size());
    session.disconnect();
}

/*
 * What an interest-rate curve actually is, from the catalogue alone.
 *
 * A swap ladder is not series that differ by `tenor`: every rung is a series
 * whose identity differs by `fwd_start`, and the tenor is the same `1D` all the
 * way up. Grouping by "the URI without its tenor" therefore answers with one
 * rung, which is what the window has been drawing. This groups by the identity
 * instead, finds the field that varies inside a group, and prints the ladder
 * that field names - the grouping the window will use.
 *
 *     ... HEDGR_LIVE_* ... hedgr.runtime.tests "[.live-curve-shape]"
 */
TEST_CASE("live curve: the rungs a curve's series name", "[.live-curve-shape]") {
    const auto* url = std::getenv("HEDGR_LIVE_URL");
    const auto* principal = std::getenv("HEDGR_LIVE_PRINCIPAL");
    const auto* password = std::getenv("HEDGR_LIVE_PASSWORD");
    REQUIRE(url != nullptr);
    REQUIRE(principal != nullptr);
    REQUIRE(password != nullptr);

    namespace transport = hedgr::session::transport;
    transport::session_options options;
    options.nats_url = url;
    if (const auto* prefix = std::getenv("HEDGR_LIVE_PREFIX"))
        options.subject_prefix = prefix;
    options.wire_format = "msgpack";
    if (const auto* ca = std::getenv("HEDGR_LIVE_CA"))
        options.tls_ca = ca;
    if (const auto* cert = std::getenv("HEDGR_LIVE_CERT"))
        options.tls_cert = cert;
    if (const auto* key = std::getenv("HEDGR_LIVE_KEY"))
        options.tls_key = key;

    transport::session_client session(options);
    REQUIRE(session.connect());
    REQUIRE(session.authenticate(principal, password).has_value());
    const auto catalogue = session.market_series(500);
    REQUIRE(catalogue.has_value());

    /* Group by everything the rung must not vary, then look at what varies. */
    std::map<std::string, std::vector<const transport::market_series_summary*>>
        groups;
    for (const auto& row : *catalogue) {
        if (row.asset_class != "IR" || row.identity.empty())
            continue;
        auto key = std::format("{}|{}|{}|{}|{}|{}|{}", row.identity.asset,
            row.identity.scope, row.identity.instrument_type,
            row.identity.quote_type, row.tenor, row.identity.party_id,
            row.identity.provider_code);
        groups[key].push_back(&row);
    }
    for (const auto& [key, rows] : groups) {
        if (rows.size() < 2)
            continue;
        std::printf("group '%s' holds %zu series\n", key.c_str(), rows.size());
        if (const auto* wanted = std::getenv("HEDGR_LIVE_LABEL");
            wanted != nullptr && key.find(wanted) == std::string::npos)
            continue;
        /* Which identity field differs among the rows of this group? */
        for (std::size_t f = 0; f < rows.front()->identity.fields.size(); ++f) {
            std::set<std::string> texts;
            for (const auto* row : rows) {
                if (f < row->identity.fields.size())
                    texts.insert(row->identity.fields[f].text);
            }
            if (texts.size() < 2)
                continue;
            std::printf("  varies: '%s' ->",
                rows.front()->identity.fields[f].name.c_str());
            for (const auto& text : texts)
                std::printf(" %s", text.c_str());
            std::printf("\n");
        }
    }

    /*
     * A label that names several rows: what tells them apart? If it is a time,
     * the browser has to show the time, because four rows under one name are
     * four curves a person cannot tell apart.
     */
    if (const auto* label = std::getenv("HEDGR_LIVE_ROW")) {
        std::size_t printed = 0;
        for (const auto& row : *catalogue) {
            if (row.label != label)
                continue;
            std::printf("row %s tenor='%s' curve_key='%s' curve_label='%s' "
                        "party='%s' provider='%s'\n",
                row.id.c_str(), row.tenor.c_str(), row.curve_key.c_str(),
                row.curve_label.c_str(), row.party_id.c_str(),
                row.provider_code.c_str());
            /* What a flag is read from: the schema's subject and the type. */
            std::printf("    identity asset='%s' subject='%s' instrument='%s' "
                        "quote='%s'\n",
                row.identity.asset.c_str(), row.identity.scope.c_str(),
                row.identity.instrument_type.c_str(),
                row.identity.quote_type.c_str());
            for (const auto& field : row.identity.fields)
                std::printf("    %s='%s'\n", field.name.c_str(),
                    field.text.c_str());
            if (++printed == 5)
                break;
        }
    }
    std::fflush(stdout);
    session.disconnect();
}
