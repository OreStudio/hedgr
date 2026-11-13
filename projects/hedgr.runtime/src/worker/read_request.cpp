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
#include "hedgr.runtime/worker/read_request.hpp"
#include "hedgr.runtime/worker/crm_names_worker.hpp"
#include "hedgr.runtime/worker/crm_worker.hpp"
#include "hedgr.runtime/worker/deployment_worker.hpp"
#include "hedgr.runtime/worker/image_map_worker.hpp"
#include "hedgr.runtime/worker/image_worker.hpp"
#include "hedgr.runtime/worker/market_worker.hpp"
#include "hedgr.runtime/worker/curve_worker.hpp"
#include "hedgr.runtime/worker/surface_worker.hpp"

namespace hedgr::runtime::worker {

namespace {

namespace event = hedgr::model::event;

/**
 * @brief A visitor set that lets the request be dispatched with one call.
 *
 * Every alternative has its own handler, so the compiler refuses a request
 * that was added without a mapping rather than dropping it at run time.
 */
template <class... Handlers>
struct overloaded final : Handlers... {
    using Handlers::operator()...;
};

}

model::event::event read_event(const read_request& request,
    const model::domain::session_key& key, std::string_view tenant_id) {
    event::event fact;
    fact.instance_id = key.instance_id;
    fact.account_id = key.account_id;
    fact.tenant_id = std::string{tenant_id};

    std::visit(overloaded{
                   [&](const image_request& value) {
                       fact.kind = event::event_kind::image_loaded;
                       fact.image_id = value.image_id;
                   },
                   [&](const image_map_request&) {
                       fact.kind = event::event_kind::currency_image_map;
                   },
                   [&](const crm_names_request& value) {
                       fact.kind = event::event_kind::crm_names;
                       fact.party_id = value.party_id;
                   },
                   [&](const crm_rates_request& value) {
                       fact.kind = event::event_kind::crm_rates;
                       fact.party_id = value.party_id;
                       fact.crm_name = value.crm_name;
                       fact.crm_reciprocal = value.reciprocal;
                   },
                   [&](const deployment_request&) {
                       fact.kind = event::event_kind::deployment_snapshot;
                   },
                   [&](const curve_request& value) {
                       fact.kind = event::event_kind::curve_sets;
                       fact.curve_key = value.key;
                       fact.curve_label = value.label;
                   },
                   [&](const surface_request& value) {
                       fact.kind = event::event_kind::vol_surface;
                       fact.blueprint_id = value.blueprint_id;
                       fact.curve_label = value.label;
                       fact.base_currency = value.base_currency;
                       fact.quote_currency = value.quote_currency;
                   },
                   [&](const catalogue_request&) {
                       fact.kind = event::event_kind::series_catalogue;
                   },
                   [&](const market_points_request& value) {
                       fact.kind = event::event_kind::series_points;
                       fact.series_id = value.series_id;
                   },
                   [&](const market_history_request& value) {
                       fact.kind = event::event_kind::series_history;
                       fact.series_id = value.series_id;
                       fact.history_from_ns = value.from_ns;
                   },
               },
        request);
    return fact;
}

model::event::event read_failed(const read_request& request,
    const model::domain::session_key& key, std::string_view tenant_id,
    std::string_view reason) {
    auto fact = read_event(request, key, tenant_id);
    fact.message = std::string{reason};
    if (std::holds_alternative<deployment_request>(request)) {
        /*
         * The deployment's handlers draw the snapshot's own failure and never
         * its message, so a failure that set only the message would leave the
         * pane showing an empty snapshot as if the read had succeeded.
         */
        fact.deployment.failure = std::string{reason};
        fact.deployment.failing_read = "session";
    }
    return fact;
}

void run_read(hedgr::session::transport::session_link& link,
    const read_request& request, model::event::event& answer) {
    std::visit(overloaded{
                   [&](const image_request& value) {
                       run_image_read(link, value.image_id, answer);
                   },
                   [&](const image_map_request&) {
                       run_image_map_read(link, answer);
                   },
                   [&](const crm_names_request& value) {
                       run_crm_names_read(link, value.party_id, answer);
                   },
                   [&](const crm_rates_request& value) {
                       run_crm_read(link, value.party_id, value.crm_name,
                           value.reciprocal, answer);
                   },
                   [&](const deployment_request&) {
                       run_deployment_read(link, answer);
                   },
                   [&](const curve_request& value) {
                       run_curve_read(link, value, answer);
                   },
                   [&](const surface_request& value) {
                       run_surface_read(link, value, answer);
                   },
                   [&](const catalogue_request&) {
                       run_market_read(link, request, answer);
                   },
                   [&](const market_points_request&) {
                       run_market_read(link, request, answer);
                   },
                   [&](const market_history_request&) {
                       run_market_read(link, request, answer);
                   },
               },
        request);
}

}
