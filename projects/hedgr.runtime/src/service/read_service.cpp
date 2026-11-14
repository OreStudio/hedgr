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
#include "hedgr.runtime/service/curve_mapping.hpp"
#include "hedgr.runtime/worker/market_worker.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <boost/log/sources/severity_feature.hpp>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hedgr::runtime::service {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace model_state = hedgr::model::state;

using condition_source = model_state::condition_source;
using support::lg;
using support::steady_now_ns;

/**
 * @brief How often an open CRM matrix is read again.
 *
 * The deployment computes the batch on demand, so this is a request rate on a
 * party's rate engine, paid once per matrix however many windows show it.
 */
constexpr std::chrono::milliseconds crm_cadence{5000};

/**
 * @brief The identity a session's catalogue holds for one series.
 *
 * A tick read wants the typed identity; the catalogue parsed it from the
 * series URI when the catalogue arrived. An empty result (no catalogue, an
 * older row, or a series the catalogue does not list) makes the read fall back
 * to the deployment's list of identities.
 */
[[nodiscard]] domain::market_series_identity catalogue_identity(
    const model_state::series_table& market, std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    const auto* catalogue = market.catalogue(instance_id, account_id);
    if (catalogue == nullptr)
        return {};
    const auto* row = catalogue->row_of(series_id);
    return row != nullptr ? row->identity : domain::market_series_identity{};
}

/**
 * @brief The same identity, scoped to the party the session acts as.
 *
 * A provider publishes one series per party, so the tick service scopes a read
 * by the caller's party and refuses an identity that names the series of four
 * parties at once (`series_ambiguous`). The catalogue row names the party the
 * series belongs to, which is not always the one the session acts as, so the
 * session's own party is what a read states. A session with no party keeps the
 * row's, because a refusal that names the ambiguity is more use than one that
 * names nothing.
 */
[[nodiscard]] domain::market_series_identity session_scoped_identity(
    domain::market_series_identity identity,
    const model_state::session_table& sessions, std::string_view instance_id,
    std::string_view account_id) {
    if (const auto session = sessions.find(instance_id, account_id)) {
        const auto party = sessions.party_id(*session);
        if (!party.empty())
            identity.party_id = std::string{party};
    }
    return identity;
}

}

read_service::read_service(const model::state::session_table& sessions,
    const model::state::window_table& windows,
    model::state::series_table& series, read_submit submit)
    : sessions_(sessions), windows_(windows),
      submit_(std::move(submit)), market_(series) {
}

void read_service::attach(std::filesystem::path database_path) {
    database_path_ = std::move(database_path);
}
std::optional<refusal> read_service::start(const read_request& request) {
    switch (request.kind) {
    case read_kind::term_structure:
        start_term_structure_read(request.window);
        return std::nullopt;
    case read_kind::vol_surface:
        start_vol_surface_read(request.window);
        return std::nullopt;
    case read_kind::crm_batch:
        start_crm_read(request.window);
        return std::nullopt;
    case read_kind::deployment:
        return start_deployment_read(windows_.id(request.window));
    case read_kind::market_catalogue:
        return start_market_catalogue_read(windows_.id(request.window));
    case read_kind::market_points:
        if (const auto* chart = std::get_if<domain::chart_state>(
                &windows_.state(request.window));
            chart != nullptr && !chart->series_id.empty()) {
            return start_market_points_read(windows_.id(request.window),
                chart->series_id);
        }
        return std::nullopt;
    case read_kind::none:
        /*
         * The window draws what the model already holds, so a refresh has
         * nothing to ask a deployment for and raising an event would only tell
         * the frame loop that a window it is already drawing is still there.
         */
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<read_service::read_session> read_service::session_read_for(
    model::state::session_handle session, read_gate gate) const {
    if (!is_open())
        return std::nullopt;
    if (gate != read_gate::token_only
        && sessions_.status(session) != domain::session_status::authenticated)
        return std::nullopt;

    /*
     * The gate still asks whether the session holds a token, because a session
     * with none cannot be resumed on the actor either. The token itself is not
     * copied: the actor holds it, and this class only names the pair.
     */
    if (sessions_.token(session).empty() && gate != read_gate::signed_in)
        return std::nullopt;

    return read_session{
        .instance_id = std::string{sessions_.instance_id(session)},
        .account_id = std::string{sessions_.account_id(session)},
        .tenant_id = std::string{sessions_.tenant_id(session)}};
}

std::optional<read_service::read_session> read_service::session_read_for(
    std::string_view instance_id, std::string_view account_id,
    read_gate gate) const {
    const auto session = sessions_.find(instance_id, account_id);
    if (!session)
        return std::nullopt;
    return session_read_for(*session, gate);
}

bool read_service::submit_read(const read_session& read,
    worker::read_request request) {
    return submit_ && submit_(read.instance_id, read.account_id, request);
}

std::optional<refusal>
read_service::start_deployment_read(std::string_view window_id) {
    if (!is_open()) {
        return refusal{{condition_source::read, std::string{window_id}},
            "cannot read the deployment: the store is not open"};
    }

    const auto window = windows_.find(window_id);
    if (!window)
        return std::nullopt;

    const auto session = windows_.session_of(*window);
    if (!session)
        return std::nullopt;

    auto read = session_read_for(*session, read_gate::signed_in_with_token);
    if (!read)
        return std::nullopt;

    const auto id = request_id::deployment(read->instance_id, read->account_id);
    if (pending_.contains(id))
        return std::nullopt;

    if (!submit_read(*read, worker::deployment_request{}))
        return std::nullopt;

    pending_.mark(id);
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a deployment read for '" << read->instance_id << "' / '"
        << read->account_id << "'";
    return std::nullopt;
}

std::optional<refusal>
read_service::start_market_catalogue_read(std::string_view window_id) {
    return start_market_read(window_id, {});
}

std::optional<refusal>
read_service::start_market_points_read(std::string_view window_id,
    std::string_view series_id) {
    return start_market_read(window_id, series_id);
}

std::optional<refusal>
read_service::start_market_read(std::string_view window_id,
    std::string_view series_id) {
    if (!is_open()) {
        return refusal{{condition_source::read, std::string{window_id}},
            "cannot read the market: the store is not open"};
    }

    const auto window = windows_.find(window_id);
    if (!window)
        return std::nullopt;

    const auto session = windows_.session_of(*window);
    if (!session)
        return std::nullopt;

    auto read = session_read_for(*session, read_gate::signed_in_with_token);
    if (!read)
        return std::nullopt;
    return launch_market_read(std::move(*read), series_id);
}

bool read_service::market_catalogue_pending(std::string_view instance_id,
    std::string_view account_id) const {
    return pending_.contains(
        request_id::catalogue(instance_id, account_id));
}

std::optional<refusal> read_service::start_market_catalogue_read(
    std::string_view instance_id, std::string_view account_id) {
    if (!is_open()) {
        return refusal{{condition_source::read,
                           std::format("{}/{}", instance_id, account_id)},
            "cannot read the market: the store is not open"};
    }

    auto read = session_read_for(instance_id, account_id,
        read_gate::signed_in_with_token);
    if (!read)
        return std::nullopt;
    return launch_market_read(std::move(*read), {});
}

std::optional<refusal> read_service::launch_market_read(
    read_session read, std::string_view series_id) {
    const bool catalogue = series_id.empty();
    const auto id = catalogue
        ? request_id::catalogue(read.instance_id, read.account_id)
        : request_id::points(read.instance_id, read.account_id, series_id);
    if (pending_.contains(id))
        return std::nullopt;

    const worker::read_request request = catalogue
        ? worker::read_request{worker::catalogue_request{}}
        : worker::read_request{worker::market_points_request{
              .series_id = std::string{series_id},
              .identity = session_scoped_identity(
                  catalogue_identity(market_, read.instance_id,
                      read.account_id, series_id),
                  sessions_, read.instance_id, read.account_id)}};
    if (!submit_read(read, request))
        return std::nullopt;

    pending_.mark(id);
    if (catalogue)
        market_.begin_catalogue(read.instance_id, read.account_id);
    else
        static_cast<void>(market_.intern(read.instance_id, read.account_id,
            series_id));

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a market " << (catalogue ? "catalogue" : "points")
        << " read for '" << read.instance_id << "' / '" << read.account_id
        << "'"
        << (catalogue ? std::string{}
                      : std::format(" series '{}'", series_id));
    return std::nullopt;
}

void read_service::start_currency_image_map(std::string_view instance_id,
    std::string_view account_id, std::string_view tenant_id) {
    if (tenant_id.empty() || !is_open())
        return;
    if (const auto* existing = currency_images_.get(std::string{tenant_id});
        existing != nullptr
        && (existing->state == model::state::image_map_state::ready
            || existing->state == model::state::image_map_state::reading))
        return;
    if (pending_.contains(request_id::currency_image_map(tenant_id)))
        return;
    auto read = session_read_for(instance_id, account_id,
        read_gate::token_only);
    if (!read)
        return;
    if (!submit_read(*read, worker::image_map_request{}))
        return;

    pending_.mark(request_id::currency_image_map(tenant_id));
    currency_images_.begin(std::string{tenant_id});
}

void read_service::retry_currency_image_map(std::string_view instance_id,
    std::string_view account_id) {
    /*
     * A refresh is also the operator's chance to retry the currency image map
     * when its first read failed; flags fall back to the drawn ones until it
     * answers.
     */
    if (const auto session = sessions_.find(instance_id, account_id);
        session && sessions_.status(*session)
            == domain::session_status::authenticated) {
        const auto tenant = std::string{sessions_.tenant_id(*session)};
        const auto* map = currency_images_.get(tenant);
        if (map == nullptr
            || map->state == model::state::image_map_state::failed)
            start_currency_image_map(instance_id, account_id, tenant);
    }
}

void read_service::start_crm_names_read(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    auto read = session_read_for(instance_id, account_id, read_gate::signed_in);
    if (!read)
        return;
    if (!submit_read(*read,
            worker::crm_names_request{.party_id = std::string{party_id}}))
        return;
}

void read_service::start_crm_read(
    model::state::window_handle handle) {
    if (!is_open() || !windows_.contains(handle))
        return;
    const auto spec = windows_.spec(handle, sessions_);
    const auto* state = std::get_if<domain::crm_matrix_state>(&spec.state);
    if (state == nullptr || !spec.session || state->party_id.empty())
        return;
    auto read = session_read_for(spec.session->instance_id,
        spec.session->account_id, read_gate::signed_in);
    if (!read)
        return;
    if (const auto* current = crm_.get(read->instance_id, read->account_id,
            state->party_id, state->crm_name, state->reciprocal);
        current != nullptr && current->reading)
        return;
    const worker::crm_rates_request request{.party_id = state->party_id,
        .crm_name = state->crm_name,
        .reciprocal = state->reciprocal};
    if (!submit_read(*read, request))
        return;
    crm_.begin(read->instance_id, read->account_id, state->party_id,
        state->crm_name, state->reciprocal);
}

void read_service::pump_crm_reads() {
    if (!is_open())
        return;
    const auto now = steady_now_ns();
    const auto& layout = windows_.layout();
    const auto kinds = windows_.kind_column();
    const auto open = windows_.open_column();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0 || open[index] == 0
            || static_cast<domain::window_kind>(kinds[index])
                != domain::window_kind::crm_matrix)
            continue;
        const model_state::window_handle window{index,
            layout.generation[index]};
        const auto session = windows_.session_of(window);
        if (!session || sessions_.status(*session)
                != domain::session_status::authenticated)
            continue;
        const auto* crm =
            std::get_if<domain::crm_matrix_state>(&windows_.state(window));
        if (crm == nullptr || crm->party_id.empty() || crm->crm_name.empty())
            continue;
        const auto* batch = crm_.get(sessions_.instance_id(*session),
            sessions_.account_id(*session), crm->party_id, crm->crm_name,
            crm->reciprocal);
        if (batch != nullptr && (batch->reading || now < batch->next_read_ns))
            continue;
        start_crm_read(window);
    }
}

void read_service::start_term_structure_read(
    model::state::window_handle handle) {
    if (!is_open() || !windows_.contains(handle))
        return;
    const auto spec = windows_.spec(handle, sessions_);
    const auto* st = std::get_if<domain::term_structure_state>(&spec.state);
    /*
     * A window with no tenor members is a curve the deployment publishes: the
     * whole ladder arrives from one call rather than a series per tenor. A
     * window with members is assembled from their ticks, and its members must
     * pair with their tenors.
     */
    const bool published = st != nullptr && st->member_ids.empty();
    const bool members_pair = published
        || (st != nullptr && st->member_ids.size() == st->member_tenors.size());
    if (st == nullptr || st->curve_key.empty() || !members_pair) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Term structure window "
            << (st == nullptr ? "holds no curve" : st->curve_key)
            << " not read: " << (st == nullptr ? 0 : st->member_ids.size())
            << " members for "
            << (st == nullptr ? 0 : st->member_tenors.size()) << " tenors";
        return;
    }
    if (!spec.session)
        return;
    auto read = session_read_for(spec.session->instance_id,
        spec.session->account_id, read_gate::signed_in);
    if (!read)
        return;

    worker::curve_request request{.key = st->curve_key,
        .label = st->label,
        .published = published,
        .member_ids = st->member_ids,
        .member_tenors = st->member_tenors};
    /*
     * A member with no tenor of its own carries the whole ladder, and the
     * deployment states that shape rather than leaving it to be inferred: the
     * evolution read declares the object's axes and every node on them. The
     * catalogue is where the series' typed identity is stated, so the request
     * takes it from there rather than from a command that never had it.
     */
    if (!published && st->member_tenors.front().empty()) {
        const auto* catalogue = market_.catalogue(read->instance_id,
            read->account_id);
        const auto* row = catalogue != nullptr
            ? catalogue->row_of(st->member_ids.front()) : nullptr;
        if (row != nullptr)
            request.member_identity = row->identity;
    }
    if (!submit_read(*read, request))
        return;
    curves_.begin(curves_.intern(read->instance_id, read->account_id,
        st->curve_key));
}

void read_service::load_term_structure(
    const model::command::command& cmd) {
    const bool published = cmd.member_ids.empty();
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Term structure '" << cmd.curve_key << "' from '" << cmd.series_id
        << "': " << (published ? std::string{"published curve"}
                               : std::format("{} members, {} tenors",
                                     cmd.member_ids.size(),
                                     cmd.member_tenors.size()));
    if (cmd.curve_key.empty()
        || (!published
            && cmd.member_ids.size() != cmd.member_tenors.size())) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Term structure '" << cmd.curve_key
            << "' not read: the members and their tenors do not pair up";
        return;
    }
    auto read = session_read_for(cmd.instance_id, cmd.account_id,
        read_gate::signed_in);
    if (!read)
        return;

    worker::curve_request request{.key = cmd.curve_key,
        .label = cmd.curve_label,
        .published = published,
        .member_ids = cmd.member_ids,
        .member_tenors = cmd.member_tenors};
    /*
     * As in start_term_structure_read: a composite member is read as the shape
     * the deployment states, which needs the series' typed identity.
     */
    if (!published && cmd.member_tenors.front().empty()) {
        const auto* catalogue = market_.catalogue(cmd.instance_id,
            cmd.account_id);
        const auto* row = catalogue != nullptr
            ? catalogue->row_of(cmd.member_ids.front()) : nullptr;
        if (row != nullptr)
            request.member_identity = row->identity;
    }
    if (!submit_read(*read, request))
        return;
    curves_.begin(curves_.intern(cmd.instance_id, cmd.account_id,
        cmd.curve_key));
}

void read_service::start_vol_surface_read(
    model::state::window_handle handle) {
    if (!is_open() || !windows_.contains(handle))
        return;
    const auto spec = windows_.spec(handle, sessions_);
    const auto* surface = std::get_if<domain::vol_surface_state>(&spec.state);
    if (surface == nullptr || surface->blueprint_id.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Volatility window "
            << (surface == nullptr ? "holds no surface" : surface->label)
            << " not read: it names no blueprint";
        return;
    }
    if (!spec.session)
        return;
    auto read = session_read_for(spec.session->instance_id,
        spec.session->account_id, read_gate::signed_in);
    if (!read)
        return;

    /*
     * The surface's own instant is the clock's now, which reads the newest
     * surface the deployment has published. A window never asks for a specific
     * instant: a desk wants the current surface, and a historical one would be
     * a different screen.
     */
    if (!submit_read(*read, worker::surface_request{
                         .blueprint_id = surface->blueprint_id,
                         .label = surface->label,
                         .base_currency = surface->base_currency,
                         .quote_currency = surface->quote_currency}))
        return;
    surfaces_.begin(surfaces_.intern(read->instance_id, read->account_id,
        surface->blueprint_id));
}

void read_service::load_vol_surface(const model::command::command& cmd) {
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Volatility surface '" << cmd.blueprint_id << "' ("
        << cmd.curve_label << ")";
    if (cmd.blueprint_id.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Volatility surface not read: the command names no blueprint";
        return;
    }
    auto read = session_read_for(cmd.instance_id, cmd.account_id,
        read_gate::signed_in);
    if (!read)
        return;

    if (!submit_read(*read, worker::surface_request{
                         .blueprint_id = cmd.blueprint_id,
                         .label = cmd.curve_label,
                         .base_currency = cmd.base_currency,
                         .quote_currency = cmd.quote_currency}))
        return;
    surfaces_.begin(surfaces_.intern(cmd.instance_id, cmd.account_id,
        cmd.blueprint_id));
}

void read_service::load_image(const model::command::command& cmd) {
    auto read = session_read_for(cmd.instance_id, cmd.account_id,
        read_gate::signed_in_with_token);
    if (!read || read->tenant_id.empty() || cmd.image_id.empty())
        return;
    const auto id = request_id::image(read->tenant_id, cmd.image_id);
    if (pending_.contains(id)
        || images_.contains(read->tenant_id, cmd.image_id)
        || failed_images_.contains(id.subject))
        return;

    if (!submit_read(*read, worker::image_request{.image_id = cmd.image_id}))
        return;

    pending_.mark(id);
}

void read_service::start_market_backfill(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    auto read = session_read_for(instance_id, account_id,
        read_gate::signed_in_with_token);
    if (!read) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Backfill for series '" << series_id << "' on '" << instance_id
            << "' / '" << account_id
            << "' not started: no signed-in session with a token";
        return;
    }

    const auto id = request_id::points(instance_id, account_id, series_id);
    if (pending_.contains(id)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Backfill for series '" << series_id
            << "' not started: one is already in flight";
        return;
    }

    const worker::market_points_request request{.series_id =
            std::string{series_id},
        .identity = session_scoped_identity(
            catalogue_identity(market_, instance_id, account_id, series_id),
            sessions_, instance_id, account_id)};
    if (!submit_read(*read, request))
        return;

    pending_.mark(id);
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Seeding live history for series '" << series_id << "' on '"
        << instance_id << "' / '" << account_id << "': first load reads the "
        << "ticks of a span widened until it holds "
        << domain::market_first_load_min_points << " points";
}

void read_service::load_history(const model::command::command& cmd) {
    const auto held = market_.find(cmd.instance_id, cmd.account_id,
        cmd.series_id);
    if (!held)
        return;

    const auto level = domain::market_level_for_span(cmd.history_span_ns);
    const auto columns = market_.columns(*held, level);
    const auto known = market_.history(*held);
    const auto plan = domain::plan_history_read(cmd.history_span_ns,
        columns.size ? columns.at_ns[0] : 0,
        columns.size ? columns.at_ns[columns.size - 1] : 0, known.floor_ns);
    if (!plan)
        return;

    const auto id = request_id::history(cmd.instance_id, cmd.account_id,
        cmd.series_id);
    if (pending_.contains(id)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "History for series '" << cmd.series_id
            << "' not started: one is already in flight";
        return;
    }

    auto read = session_read_for(cmd.instance_id, cmd.account_id,
        read_gate::signed_in_with_token);
    if (!read) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "History for series '" << cmd.series_id
            << "' not started: no signed-in session with a token";
        return;
    }

    const worker::market_history_request request{.series_id = cmd.series_id,
        .identity = session_scoped_identity(
            catalogue_identity(market_, cmd.instance_id, cmd.account_id,
                cmd.series_id),
            sessions_, cmd.instance_id, cmd.account_id),
        .from_ns = plan->from_ns,
        .to_ns = plan->to_ns};
    if (!submit_read(*read, request))
        return;

    pending_.mark(id);
    market_.begin_history(*held);
    ++history_reads_started_;
    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Reading history for series '" << cmd.series_id << "': span_ns="
        << cmd.history_span_ns << " level=" << static_cast<int>(plan->level)
        << " held_oldest_at_ns=" << plan->to_ns
        << " from_ns=" << plan->from_ns << " max_ticks="
        << domain::market_tick_ceiling;
}

void read_service::load_crm_names(const model::command::command& cmd) {
    start_crm_names_read(cmd.instance_id, cmd.account_id, cmd.party_id);
}

bool read_service::apply(const model::event::event& ev) {
    if (const auto answered = request_id::answered_by(ev))
        pending_.clear(*answered);
    switch (ev.kind) {
    case event::event_kind::series_catalogue: {
        if (ev.message.empty()) {
            market_.apply_catalogue(ev.instance_id, ev.account_id,
                ev.series, ev.providers);
            market_.apply_published_curves(ev.instance_id, ev.account_id,
                ev.published_curves, ev.published_curves_error);
            market_.apply_surface_blueprints(ev.instance_id, ev.account_id,
                ev.surface_blueprints, ev.surface_blueprints_error);
            market_.apply_instruments(ev.instance_id, ev.account_id,
                ev.instruments, ev.instruments_error);
        } else {
            market_.fail_catalogue(ev.instance_id, ev.account_id,
                ev.message);
        }
        return true;
    }
    case event::event_kind::crm_names: {
        crm_.set_names(ev.instance_id, ev.account_id, ev.party_id,
            ev.crm_names);
        return true;
    }
    case event::event_kind::crm_rates: {
        if (ev.message.empty())
            crm_.set(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
                ev.crm_reciprocal, ev.crm_cells, ev.crm_conventions,
                ev.crm_as_of_ns, ev.crm_latency_ns);
        else
            crm_.fail(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
                ev.crm_reciprocal, ev.message);
        crm_.schedule(ev.instance_id, ev.account_id, ev.party_id, ev.crm_name,
            ev.crm_reciprocal, steady_now_ns()
                + std::chrono::duration_cast<std::chrono::nanoseconds>(
                    crm_cadence).count());
        return true;
    }
    case event::event_kind::curve_sets: {
        const auto handle = curves_.intern(ev.instance_id, ev.account_id,
            ev.curve_key);
        service::apply_curve_event(curves_, handle, ev);
        return true;
    }
    case event::event_kind::vol_surface: {
        const auto handle = surfaces_.intern(ev.instance_id, ev.account_id,
            ev.blueprint_id);
        if (ev.message.empty()) {
            surfaces_.apply(handle, ev.curve_label, ev.base_currency,
                ev.quote_currency, ev.surface_as_of_ns, ev.surface_expiries,
                ev.surface_strikes, ev.surface_values);
        } else {
            surfaces_.fail(handle, ev.message);
        }
        return true;
    }
    case event::event_kind::image_loaded: {
        const auto key = model::state::make_tenant_key(ev.tenant_id,
            ev.image_id);
        if (ev.message.empty()) {
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Image '" << ev.image_id << "' for tenant '" << ev.tenant_id
                << "' read: " << ev.bytes.size() << " bytes of '" << ev.mime_type
                << "'";
            if (images_.put(ev.tenant_id, ev.image_id,
                    model::state::image_content{.mime_type = ev.mime_type,
                        .data = ev.bytes})) {
                failed_images_.erase(key);
            } else {
                failed_images_.insert(key);
            }
        } else {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Image '" << ev.image_id << "' for tenant '" << ev.tenant_id
                << "' could not be read: " << ev.message;
            failed_images_.insert(key);
        }
        return true;
    }
    case event::event_kind::currency_image_map: {
        if (ev.message.empty()) {
            std::vector<std::pair<std::string, std::string>> mapped;
            for (const auto& image : ev.image_refs) {
                mapped.emplace_back(image.currency, image.image_id);
            }
            currency_images_.set(ev.tenant_id, std::move(mapped));
        } else {
            currency_images_.fail(ev.tenant_id, ev.message);
        }
        return true;
    }
    case event::event_kind::series_points: {
        const auto held =
            market_.find(ev.instance_id, ev.account_id, ev.series_id);
        if (!held) {
            ++unheld_points_;
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Dropped " << ev.points.size() << " points for series '"
                << ev.series_id << "': no row is held for it";
            return true;
        }
        if (ev.message.empty()) {
            market_.seed(*held, ev.points, ev.observed_at_ns);
            const auto fine = market_.columns(*held,
                model::domain::market_level::fine);
            const auto medium = market_.columns(*held,
                model::domain::market_level::medium);
            const auto coarse = market_.columns(*held,
                model::domain::market_level::coarse);
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Seeded series '" << ev.series_id << "' from "
                << ev.points.size() << " observations: fine=" << fine.size
                << " medium=" << medium.size << " coarse=" << coarse.size
                << " points held"
                << (fine.size ? std::format(" (fine oldest_at_ns={} "
                                            "newest_at_ns={})",
                                    fine.at_ns[0], fine.at_ns[fine.size - 1])
                              : std::string{});
        } else {
            market_.fail(*held, ev.message, ev.observed_at_ns);
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Backfill for series '" << ev.series_id
                << "' failed: " << ev.message;
        }
        return true;
    }
    case event::event_kind::series_history: {
        const auto held =
            market_.find(ev.instance_id, ev.account_id, ev.series_id);
        if (!held) {
            ++unheld_points_;
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Dropped " << ev.points.size()
                << " history points for series '" << ev.series_id
                << "': no row is held for it";
            return true;
        }
        if (!ev.message.empty()) {
            market_.abandon_history(*held);
            market_.fail(*held, ev.message, ev.observed_at_ns);
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "History for series '" << ev.series_id
                << "' failed: " << ev.message;
            return true;
        }
        market_.seed(*held, ev.points, ev.observed_at_ns);
        market_.finish_history(*held, ev.history_from_ns, ev.history_truncated);
        const char* outcome = ev.history_truncated
            ? "the reply was at the ceiling or a retry shortened the span, older ticks were not read"
            : ev.points.empty()
                || ev.points.front().at_ns > ev.history_from_ns
                ? "the server holds nothing older"
                : "the span is covered";
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "History for series '" << ev.series_id << "' seeded from "
            << ev.points.size() << " observations: from_ns="
            << ev.history_from_ns << " outcome='" << outcome << "'"
            << (ev.points.empty()
                    ? std::string{}
                    : std::format(" oldest_at_ns={}", ev.points.front().at_ns));
        return true;
    }
    case event::event_kind::deployment_snapshot:
        /* The snapshot itself belongs on the session row, applied below. */
        return false;
    default:
        return false;
    }
}

void read_service::clear() {
    market_.clear();
    currency_images_.clear();
    images_.clear();
    failed_images_.clear();
    curves_.clear();
    crm_.clear();
    pending_.clear_all();
}

}
