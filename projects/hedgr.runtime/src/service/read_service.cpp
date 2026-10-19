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
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.runtime/worker/crm_names_worker.hpp"
#include "hedgr.runtime/worker/crm_worker.hpp"
#include "hedgr.runtime/worker/deployment_worker.hpp"
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.runtime/worker/image_worker.hpp"
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/term_structure_worker.hpp"
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
using manager::connection_options_for;
using support::lg;
using support::steady_now_ns;

/**
 * @brief How often an open CRM matrix is read again.
 *
 * The deployment computes the batch on demand, so this is a request rate on a
 * party's rate engine, paid once per matrix however many windows show it.
 */
constexpr std::chrono::milliseconds crm_cadence{5000};
}

read_service::read_service(boost::asio::io_context& pool,
    model::command::event_queue& events,
    const model::state::instance_table& instances,
    const model::state::account_table& accounts,
    const model::state::session_table& sessions,
    const model::state::window_table& windows,
    const session::transport::session_options& seed)
    : pool_(pool), events_(events), instances_(instances),
      accounts_(accounts), sessions_(sessions), windows_(windows),
      seed_(seed) {
}

void read_service::attach(std::filesystem::path database_path) {
    database_path_ = std::move(database_path);
}
std::optional<refusal> read_service::start(const read_request& request) {
    switch (request.kind) {
    case read_kind::term_structure:
        start_term_structure_read(request.window);
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
    case read_kind::placeholder: {
        /*
         * A refresh has nothing real to ask for yet, so the request is turned
         * into an event carrying the window id and nothing else, which is
         * enough to exercise the path from a command through the event
         * channel and back into apply(). The real handler replaces the
         * payload, not the shape.
         */
        event::event fact;
        fact.kind = event::event_kind::window_data;
        fact.payload = std::string{windows_.id(request.window)};
        events_.push(std::move(fact));
        return std::nullopt;
    }
    }
    return std::nullopt;
}

std::optional<worker::session_read> read_service::session_read_for(
    model::state::session_handle session, read_gate gate) const {
    if (!is_open())
        return std::nullopt;
    if (gate != read_gate::token_only
        && sessions_.status(session) != domain::session_status::authenticated)
        return std::nullopt;

    worker::session_read read;
    read.token = std::string{sessions_.token(session)};
    if (read.token.empty() && gate != read_gate::signed_in)
        return std::nullopt;

    read.instance_id = std::string{sessions_.instance_id(session)};
    read.account_id = std::string{sessions_.account_id(session)};
    read.tenant_id = std::string{sessions_.tenant_id(session)};
    const auto instance = instances_.find(read.instance_id);
    if (!instance)
        return std::nullopt;

    const auto account = accounts_.find(read.account_id);
    read.principal =
        account ? accounts_.get(*account)->principal : std::string{};
    read.options = connection_options_for(*instances_.get(*instance),
        seed_, database_path_);
    return read;
}

std::optional<worker::session_read> read_service::session_read_for(
    std::string_view instance_id, std::string_view account_id,
    read_gate gate) const {
    const auto session = sessions_.find(instance_id, account_id);
    if (!session)
        return std::nullopt;
    return session_read_for(*session, gate);
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

    worker::deployment_job job;
    job.options = std::move(read->options);
    job.key = domain::session_key{read->instance_id, read->account_id};
    job.token = std::move(read->token);
    job.principal = std::move(read->principal);

    pending_.mark(id);
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a deployment read for '" << read->instance_id << "' / '"
        << read->account_id << "'";
    worker::spawn_deployment_read(pool_, std::move(job), events_);
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

    const bool catalogue = series_id.empty();
    const auto id = catalogue
        ? request_id::catalogue(read->instance_id, read->account_id)
        : request_id::points(read->instance_id, read->account_id, series_id);
    if (pending_.contains(id))
        return std::nullopt;

    worker::market_job job;
    job.options = std::move(read->options);
    job.key = domain::session_key{read->instance_id, read->account_id};
    job.token = std::move(read->token);
    job.principal = std::move(read->principal);
    job.catalogue = catalogue;
    job.series_id = std::string{series_id};

    pending_.mark(id);
    if (catalogue)
        market_.begin_catalogue(read->instance_id, read->account_id);

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Starting a market " << (catalogue ? "catalogue" : "points")
        << " read for '" << read->instance_id << "' / '" << read->account_id
        << "'"
        << (catalogue ? std::string{}
                      : std::format(" series '{}'", series_id));
    worker::spawn_market_read(pool_, std::move(job), events_);
    return std::nullopt;
}

void read_service::start_currency_image_map(const model::event::event& ev) {
    if (ev.tenant_id.empty() || !is_open())
        return;
    if (const auto* existing = currency_images_.get(ev.tenant_id);
        existing != nullptr
        && (existing->state == model::state::image_map_state::ready
            || existing->state == model::state::image_map_state::reading))
        return;
    if (pending_.contains(request_id::currency_image_map(ev.tenant_id)))
        return;
    auto read = session_read_for(ev.instance_id, ev.account_id,
        read_gate::token_only);
    if (!read)
        return;

    worker::image_map_job job;
    job.session = std::move(*read);
    job.session.tenant_id = ev.tenant_id;
    pending_.mark(request_id::currency_image_map(ev.tenant_id));
    currency_images_.begin(ev.tenant_id);
    worker::spawn_image_map_read(pool_, std::move(job), events_);
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
            || map->state == model::state::image_map_state::failed) {
            model::event::event retry;
            retry.instance_id = std::string{instance_id};
            retry.account_id = std::string{account_id};
            retry.tenant_id = tenant;
            start_currency_image_map(retry);
        }
    }
}

void read_service::start_crm_names_read(std::string_view instance_id,
    std::string_view account_id, std::string_view party_id) {
    auto read = session_read_for(instance_id, account_id, read_gate::signed_in);
    if (!read)
        return;
    worker::crm_names_job job;
    job.session = std::move(*read);
    job.party_id = std::string{party_id};
    worker::spawn_crm_names_read(pool_, std::move(job), events_);
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
    worker::crm_job job;
    job.session = std::move(*read);
    job.party_id = state->party_id;
    job.crm_name = state->crm_name;
    job.reciprocal = state->reciprocal;
    if (const auto* current = crm_.get(job.session.instance_id,
            job.session.account_id, state->party_id, state->crm_name,
            state->reciprocal);
        current != nullptr && current->reading)
        return;
    crm_.begin(job.session.instance_id, job.session.account_id,
        state->party_id, state->crm_name, state->reciprocal);
    worker::spawn_crm_read(pool_, std::move(job), events_);
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
    if (st == nullptr || st->curve_key.empty() || st->member_ids.empty()
        || st->member_ids.size() != st->member_tenors.size())
        return;
    if (!spec.session)
        return;
    auto read = session_read_for(spec.session->instance_id,
        spec.session->account_id, read_gate::signed_in);
    if (!read)
        return;

    worker::term_structure_job job;
    job.session = std::move(*read);
    job.key = st->curve_key;
    job.label = st->label;
    job.member_ids = st->member_ids;
    job.member_tenors = st->member_tenors;
    term_structures_.begin(job.session.instance_id, job.session.account_id,
        st->curve_key, st->label);
    worker::spawn_term_structure_read(pool_, std::move(job), events_);
}

void read_service::load_term_structure(
    const model::command::command& cmd) {
    if (cmd.curve_key.empty()
        || cmd.member_ids.size() != cmd.member_tenors.size()
        || cmd.member_ids.empty())
        return;
    auto read = session_read_for(cmd.instance_id, cmd.account_id,
        read_gate::signed_in);
    if (!read)
        return;

    worker::term_structure_job job;
    job.session = std::move(*read);
    job.key = cmd.curve_key;
    job.label = cmd.curve_label;
    job.member_ids = cmd.member_ids;
    job.member_tenors = cmd.member_tenors;
    term_structures_.begin(cmd.instance_id, cmd.account_id, cmd.curve_key,
        cmd.curve_label);
    worker::spawn_term_structure_read(pool_, std::move(job), events_);
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

    worker::image_job job;
    job.session = std::move(*read);
    job.image_id = cmd.image_id;
    pending_.mark(id);
    worker::spawn_image_read(pool_, std::move(job), events_);
}

void read_service::start_market_backfill(std::string_view instance_id,
    std::string_view account_id, std::string_view series_id) {
    auto read = session_read_for(instance_id, account_id,
        read_gate::signed_in_with_token);
    if (!read)
        return;

    const auto id = request_id::points(instance_id, account_id, series_id);
    if (pending_.contains(id))
        return;

    worker::market_job job;
    job.options = std::move(read->options);
    job.key = domain::session_key{std::string{instance_id},
        std::string{account_id}};
    job.token = std::move(read->token);
    job.principal = std::move(read->principal);
    job.catalogue = false;
    job.series_id = std::string{series_id};
    job.limit = 2000;

    pending_.mark(id);
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Seeding live history for series '" << series_id << "' on '"
        << instance_id << "' / '" << account_id << "'";
    worker::spawn_market_read(pool_, std::move(job), events_);
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
            market_.set_catalogue(ev.instance_id, ev.account_id,
                ev.series);
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
    case event::event_kind::term_structure: {
        if (ev.message.empty()) {
            term_structures_.set(ev.instance_id, ev.account_id, ev.curve_key,
                ev.curve_label, ev.term_curves);
        } else {
            term_structures_.fail(ev.instance_id, ev.account_id, ev.curve_key,
                ev.message);
        }
        return true;
    }
    case event::event_kind::image_loaded: {
        const auto key = model::state::make_tenant_key(ev.tenant_id,
            ev.image_id);
        if (ev.message.empty()) {
            if (images_.put(ev.tenant_id, ev.image_id,
                    model::state::image_content{.mime_type = ev.mime_type,
                        .data = ev.bytes})) {
                failed_images_.erase(key);
            } else {
                failed_images_.insert(key);
            }
        } else {
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
        if (ev.message.empty()) {
            market_.set_points(ev.instance_id, ev.account_id, ev.series_id,
                ev.points, ev.observed_at_ns);
        } else {
            market_.fail_points(ev.instance_id, ev.account_id, ev.series_id,
                ev.message, ev.observed_at_ns);
        }
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
    term_structures_.clear();
    crm_.clear();
    pending_.clear_all();
}

}
