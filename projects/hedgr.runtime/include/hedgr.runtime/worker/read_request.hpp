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
#ifndef HEDGR_RUNTIME_WORKER_READ_REQUEST_HPP
#define HEDGR_RUNTIME_WORKER_READ_REQUEST_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hedgr::runtime::worker {

/** @brief One tenant image, by its identifier. */
struct image_request final {
    /** @brief The image the tenant holds. */
    std::string image_id;
};

/** @brief The tenant's currency-to-image map. */
struct image_map_request final {
};

/** @brief The enabled CRM names one party has configured. */
struct crm_names_request final {
    /** @brief The party whose names are read. */
    std::string party_id;
};

/** @brief One party's cross-rates batch. */
struct crm_rates_request final {
    /** @brief The party whose matrix is read. */
    std::string party_id;

    /** @brief One CRM to scope to, or empty for every enabled CRM. */
    std::string crm_name;

    /** @brief Whether to ask the deployment for reciprocal backfill. */
    bool reciprocal = false;
};

/** @brief The deployment's tenant counts and roster. */
struct deployment_request final {
};

/** @brief One curve, named as its tenor members. */
struct term_request final {
    /** @brief The curve's key, as the browser holds it. */
    std::string key;

    /** @brief The curve's label, for the event and its log line. */
    std::string label;

    /** @brief The series of each tenor, in the order the table shows them. */
    std::vector<std::string> member_ids;

    /** @brief The tenor each member is plotted under. */
    std::vector<std::string> member_tenors;
};

/** @brief The series the deployment publishes, and their providers. */
struct catalogue_request final {
};

/** @brief One series' newest ticks, for a chart's first load. */
struct market_points_request final {
    /** @brief The series' key. */
    std::string series_id;

    /**
     * @brief The typed identity the catalogue gave this series.
     *
     * The read names the series by this rather than listing the deployment's
     * identities first. Empty when the catalogue did not carry one, and the
     * read then falls back to the list.
     */
    model::domain::market_series_identity identity;
};

/** @brief One series' ticks over a span the chart planned. */
struct market_history_request final {
    /** @brief The series' key. */
    std::string series_id;

    /** @brief The typed identity the catalogue gave this series. */
    model::domain::market_series_identity identity;

    /** @brief The earliest instant wanted, epoch nanoseconds. */
    std::int64_t from_ns = 0;

    /** @brief The end of the span, exclusive, epoch nanoseconds. */
    std::int64_t to_ns = 0;
};

/**
 * @brief One read a session asks for, carrying every input it needs.
 *
 * A read used to be a closure built at the call site, which made the request
 * invisible to anything that wanted to see it: a queue could hold the closure
 * but not say what was queued. The variant is the request itself, so a queue
 * holds a value, a log can name it, and the actor that runs it needs nothing
 * from the caller's frame.
 *
 * Every read a window makes is here, so the actor's connection carries the
 * whole of what a session asks for and no read opens a connection of its own.
 */
using read_request = std::variant<image_request, image_map_request,
    crm_names_request, crm_rates_request, deployment_request, term_request,
    catalogue_request, market_points_request, market_history_request>;

/**
 * @brief The event a request will answer with, before the read runs.
 *
 * The kind, the pair it belongs to and the fields that name what was asked for
 * come from here, so every read path builds its answer the one way and a
 * failed read fills the same event the successful one would have. The tenant
 * is carried because the image kinds key their cache by it.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT model::event::event read_event(
    const read_request& request, const model::domain::session_key& key,
    std::string_view tenant_id);

/**
 * @brief The event that answers a request which could not run.
 *
 * The same event @c read_event names, carrying @p reason where its handlers
 * look for a failure: the message, and the deployment's snapshot failure,
 * which is the field its pane draws rather than the message.
 */
[[nodiscard]] HEDGR_RUNTIME_EXPORT model::event::event read_failed(
    const read_request& request, const model::domain::session_key& key,
    std::string_view tenant_id, std::string_view reason);

/**
 * @brief Runs one request on an open connection and fills its event.
 *
 * The connection is already resumed, so this makes no call of its own to the
 * transport's lifecycle: a failed read fills the event's own failure field and
 * a link that is down is the caller's session event, not a read answer.
 */
HEDGR_RUNTIME_EXPORT void run_read(
    hedgr::session::transport::session_link& link,
    const read_request& request, model::event::event& answer);

}

#endif
