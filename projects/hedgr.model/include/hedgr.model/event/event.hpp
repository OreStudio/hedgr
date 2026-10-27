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
#ifndef HEDGR_MODEL_EVENT_EVENT_HPP
#define HEDGR_MODEL_EVENT_EVENT_HPP

#include "hedgr.model/domain/crm.hpp"
#include "hedgr.model/domain/deployment.hpp"
#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/password_policy.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace hedgr::model::event {

/**
 * @brief What happened on a worker and needs to reach the frame loop.
 *
 * Events are the only thing that crosses from a pool thread to the UI thread.
 * A worker never touches a state table; it posts an event, and the frame loop
 * applies it. That is the whole concurrency contract.
 */
enum class event_kind : std::uint8_t {
    /** @brief The session moved to a new lifecycle state. */
    session_status_changed = 0,
    /** @brief IAM accepted the account and returned a token. */
    session_authenticated = 1,
    /** @brief The attempt failed; code and message say why. */
    session_failed = 2,
    /** @brief A window's data was refreshed and is ready to draw. */
    window_data = 4,
    /** @brief A probe finished, whatever it found. */
    probe_result = 5,
    /**
     * @brief The master password was derived and verified, and is now held.
     *
     * The password that was accepted travels in @c master_password, a
     * @c secret_string that wipes itself, and the frame thread moves it into
     * the held secret.
     */
    store_unlocked = 6,
    /**
     * @brief An attempt to unlock the store was refused.
     *
     * The same state as @c store_lock::failed, reported so the interface can
     * put the sentence beside the prompt. The message names the state and not
     * the secret, so an operator reading a log learns that an attempt failed
     * and nothing about what was tried.
     */
    store_lock_failed = 7,
    /**
     * @brief A deployment answered the password-policy read, or did not.
     *
     * Reported even when the read failed, because "asked and did not answer"
     * is a state the create prompt has to draw differently from "not asked
     * yet": one is a wait and the other is the absence of a policy, and a
     * screen that drew them the same would be claiming rules the deployment
     * never stated.
     */
    password_policy_fetched = 8,
    /**
     * @brief A deployment read finished, successfully or not.
     *
     * One kind for both outcomes because the snapshot carries its own failure,
     * and a window that received the success kind alone would have no way to
     * stop showing a read in flight.
     */
    deployment_snapshot = 9,
    /** @brief The market series catalogue was read. */
    series_catalogue = 10,
    /** @brief One series' observations were read. */
    series_points = 11,
    /** @brief A tenant's currency-to-image map was read. */
    currency_image_map = 12,
    /** @brief One tenant image was read by identifier, or could not be. */
    image_loaded = 13,
    /** @brief One interest-rate curve was read, as its latest term nodes. */
    term_structure = 14,
    /** @brief A party's cross-rates batch was read. */
    crm_rates = 15,
    /** @brief The enabled CRM names a party has configured. */
    crm_names = 16,
    /** @brief IAM issued a replacement token for a live session. */
    session_token_refreshed = 17,
    /**
     * @brief A pool thread sealed a new master password's verification blob.
     *
     * The blob is in @c payload and the password that produced it in
     * @c master_password. The frame thread writes the row and then holds the
     * password, so the store write never happens on the pool.
     */
    master_sealed = 18,
    /**
     * @brief A series' older observations were read, or the read failed.
     *
     * Answers the deeper read a chart span asked for, which has its own mark
     * apart from the first backfill's @c series_points.
     */
    series_history = 19,
    /**
     * @brief A live session was re-scoped to another party.
     *
     * Carries the fresh, party-scoped token and the tenant and party IAM
     * answered with. The frame loop replaces the session's token and party,
     * reopens the live stream for the new party, and re-reads the catalogue.
     */
    session_party_changed = 20,
    /**
     * @brief The instance refused to scope a live session to a party.
     *
     * The session stays signed in with its old token; the refusal is reported
     * as a condition so the operator sees why the control went back.
     */
    session_party_refused = 21,
};

/** @brief The canonical text of an event kind, for logs. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(event_kind value) noexcept;

/**
 * @brief One fact from a worker, flat and self-contained.
 *
 * The struct is deliberately wider than any single kind needs. Events are
 * occasional rather than hot, so padding them to one shape keeps the queue a
 * contiguous array of one type instead of a variant with an allocation per
 * alternative.
 */
struct event final {
    /** @brief What happened. */
    event_kind kind = event_kind::session_status_changed;

    /** @brief Instance the fact belongs to. */
    std::string instance_id;

    /** @brief Account the fact belongs to. */
    std::string account_id;

    /**
     * @brief Which sign-in attempt this fact belongs to, zero for no attempt.
     *
     * A pair can have two workers for a moment: a second sign-in asks the
     * first to stop, and the first may still post its own outcome before it
     * ends. The attempt is what lets the frame loop keep the newer worker's
     * facts and drop the older one's, so a stopped worker cannot report a
     * disconnect over a session that has just signed in again.
     */
    std::uint64_t attempt = 0;

    /** @brief New status, for session_status_changed. */
    domain::session_status status = domain::session_status::disconnected;

    /** @brief Round-trip time of the request that produced the fact, in microseconds. */
    std::int64_t latency_us = 0;

    /** @brief Stable error code, for session_failed. */
    std::string code;

    /** @brief Human-readable error, for session_failed. */
    std::string message;

    /** @brief Tenant IAM resolved, for session_authenticated. */
    std::string tenant_name;

    /** @brief Key of the tenant IAM resolved, for session_authenticated. */
    std::string tenant_id;

    /**
     * @brief The context the session runs in, for session_authenticated.
     *
     * Derived from the tenant IAM resolved, at the boundary, so the rest of the
     * process reads a decision rather than re-applying the deployment's rule.
     */
    domain::session_mode mode = domain::session_mode::application;

    /**
     * @brief The deployment read, for deployment_snapshot.
     *
     * Carried whole because the window draws every field of it in one frame,
     * and a snapshot split into per-counter events could be drawn half old and
     * half new.
     */
    domain::deployment_snapshot deployment;

    /** @brief The series catalogue, for series_catalogue. */
    std::vector<domain::market_series_row> series;

    /**
     * @brief The deployment's providers, for series_catalogue.
     *
     * Empty when the provider read failed; the browser then hides no provider
     * rather than hiding the wrong rows.
     */
    std::vector<domain::market_provider_row> providers;

    /** @brief The series a points event is about, for series_points. */
    std::string series_id;

    /** @brief The points, for series_points. */
    std::vector<domain::market_point> points;

    /** @brief How far back a history read asked, epoch ns, for series_history. */
    std::int64_t history_from_ns = 0;

    /**
     * @brief Whether the reply was at the ceiling or a retry shortened the
     * span, for series_points and series_history.
     */
    bool history_truncated = false;

    /** @brief The span the tick read answered, epoch ns, zero end for now. */
    std::int64_t tick_from_ns = 0;
    std::int64_t tick_to_ns = 0;

    /** @brief The coordinate plotted, and how many the reply carried. */
    std::string tick_coordinate;
    std::size_t tick_coordinates = 0;

    /** @brief How many ticks the reply carried, of every coordinate. */
    std::size_t ticks_read = 0;

    /** @brief When the read finished, on the monotonic clock, for market events. */
    std::int64_t observed_at_ns = 0;

    /** @brief Party IAM resolved, for session_authenticated. */
    std::string party_name;

    /** @brief Key of the party IAM resolved, for session_authenticated. */
    std::string party_id;

    /** @brief Bearer token, for session_authenticated. */
    domain::secret_string token;

    /** @brief The master password to hold, for store_unlocked and master_sealed. */
    domain::secret_string master_password;

    /** @brief Media type of the image, for image_loaded. */
    std::string mime_type;

    /** @brief Image identifier, for image_loaded. */
    std::string image_id;

    /** @brief The curve identity, for term_structure. */
    std::string curve_key;

    /** @brief The curve's name, for term_structure. */
    std::string curve_label;

    /** @brief The recent curves, oldest first, for term_structure. */
    std::vector<domain::term_curve> term_curves;

    /** @brief The batch's cells, for crm_rates. */
    std::vector<domain::crm_cell> crm_cells;

    /** @brief The batch's newest as-of in UTC nanoseconds, for crm_rates. */
    std::int64_t crm_as_of_ns = 0;

    /** @brief The CRM scope the window asked for, for crm_rates. */
    std::string crm_name;

    /** @brief Whether the window asked for reciprocal backfill. */
    bool crm_reciprocal = false;

    /** @brief Spot display conventions for the batch's pairs. */
    std::vector<domain::crm_convention> crm_conventions;

    /** @brief How long the batch read took, for the matrix's latency. */
    std::int64_t crm_latency_ns = 0;

    /** @brief The enabled CRM names, for crm_names. */
    std::vector<std::string> crm_names;

    /** @brief The image bytes, for image_loaded. */
    std::vector<std::uint8_t> bytes;

    /** @brief One currency and the image it names, for currency_image_map. */
    struct image_ref final {
        /** @brief The currency's ISO code. */
        std::string currency;
        /** @brief The image the currency names. */
        std::string image_id;
    };

    /** @brief The tenant's currency-to-image map, for currency_image_map. */
    std::vector<image_ref> image_refs;

    /**
     * @brief The parties the login may act as, for session_authenticated.
     *
     * Serialised as id, unit separator, name, record separator, repeated. The
     * event crosses components that may not share a container type, and the
     * list is a handful of short strings.
     */
    std::string parties;

    /** @brief The payload, for window_data. */
    std::string payload;

    /** @brief What the broker answered, for probe_result. */
    domain::reachability reach = domain::reachability::unknown;

    /** @brief What IAM answered, for probe_result. */
    domain::readiness ready = domain::readiness::unknown;

    /**
     * @brief Whether the bootstrap read returned, for probe_result.
     *
     * The structured answer below is only meaningful when this is set. A
     * broker that never opened and an IAM that never answered both leave it
     * clear, and the two are told apart from a deployment that answered by
     * this flag rather than by reading a default false as an answer.
     */
    bool bootstrap_answered = false;

    /**
     * @brief The deployment's own bootstrap answer, for probe_result.
     *
     * Carried whole rather than flattened into the @c detail sentence, because
     * the pane shows each field on its own row. A field the deployment did not
     * state is left absent inside this, so the pane draws no row for it.
     */
    domain::bootstrap_status bootstrap;

    /**
     * @brief Where the lock stands, for the store_* kinds.
     *
     * Sent rather than inferred so the interface draws the state the runtime
     * actually holds, including @c failed, rather than the state it hoped for
     * when it pushed the command.
     */
    domain::store_lock lock = domain::store_lock::absent;

    /**
     * @brief The low-level cause, for probe_result.
     *
     * A field of its own rather than @c payload, because payload is documented
     * as a data payload and this is the sentence that explains an unhealthy
     * instance: the subject with no responder, the address that refused.
     */
    std::string detail;

    /**
     * @brief Whether the deployment stated a policy, for
     * password_policy_fetched.
     *
     * False for a read that was refused or never answered, which the create
     * prompt draws as "strength cannot be assessed" rather than as a policy
     * that requires nothing. @c message says which of the two it was.
     */
    bool policy_available = false;

    /** @brief The policy's rules, for password_policy_fetched. */
    domain::password_policy policy;
};

}

#endif
