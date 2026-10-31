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
#ifndef HEDGR_RUNTIME_SERVICE_READ_SERVICE_HPP
#define HEDGR_RUNTIME_SERVICE_READ_SERVICE_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/command/command.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/crm_table.hpp"
#include "hedgr.model/state/image_cache.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.model/state/tenant_image_map.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include "hedgr.model/state/term_structure_table.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.runtime/service/pending_reads.hpp"
#include "hedgr.runtime/service/window_service.hpp"
#include "hedgr.runtime/worker/read_request.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace hedgr::runtime::service {

/**
 * @brief The reads a session makes on behalf of a window, and what each
 * answer fills.
 *
 * Owns the market, currency image, image, term structure and CRM tables with
 * the in-flight mark of every read that has no row of its own to say it is
 * running. A starter checks the session, marks the read and asks the session's
 * actor to run it, which answers with one event; @c apply writes the answer on
 * the frame thread, so the actor never touches a table and this class never
 * holds a connection.
 *
 * The catalogue and the CRM batches are scheduled here too: a batch owns its
 * deadline, and the @c crm_rates answer is what moves it.
 *
 * Holds no reference to the manager. The sessions, windows and rows it reads
 * are passed in as const references, and the one series table the live feed
 * also writes is handed out by @c market().
 */
class HEDGR_RUNTIME_EXPORT read_service final {
public:
    /**
     * @brief Asks a session's actor to run one read.
     *
     * True when the read was queued, and then an answer event follows. False
     * when the session has no actor or it is stopping, and then nothing is in
     * flight and the caller must not mark one.
     */
    using read_submit = std::function<bool(std::string_view instance_id,
        std::string_view account_id, const worker::read_request& request)>;

    /**
     * @param submit the session's actors. Empty means no read is run on one,
     * which is what a test that only drives the tables asks for.
     *
     * There is no event queue: every read this service starts is answered
     * through the actor it was handed to, and the one event the queue used to
     * carry - a window id with nothing in it, for kinds that have no read - is
     * gone with the kinds' cadences.
     */
    read_service(const model::state::session_table& sessions,
        const model::state::window_table& windows,
        model::state::series_table& series, read_submit submit = {});

    /**
     * @brief Records where the store is, which is what opens the reads.
     *
     * A read connects with the options its instance row builds, and that needs
     * the store path, so none starts before this.
     */
    void attach(std::filesystem::path database_path);

    /** @brief The market series, their catalogues and the points each holds. */
    [[nodiscard]] const model::state::series_table& market() const noexcept {
        return market_;
    }

    /** @brief The same, writable, for the feed that appends closed buckets. */
    [[nodiscard]] model::state::series_table& market() noexcept {
        return market_;
    }

    /**
     * @brief Backfill answers dropped because nothing held their series.
     *
     * A read that outlives the window that asked for it must not grow the
     * table, so an answer for a series that is no longer interned is counted
     * and not stored.
     */
    [[nodiscard]] std::size_t unheld_points() const noexcept {
        return unheld_points_;
    }

    /** @brief Which image each tenant currency names. */
    [[nodiscard]] const model::state::tenant_image_map&
    currency_images() const noexcept {
        return currency_images_;
    }

    /** @brief The latest CRM batch each session and party read. */
    [[nodiscard]] const model::state::crm_table& crm() const noexcept {
        return crm_;
    }

    /** @brief The latest curve each browser session read. */
    [[nodiscard]] const model::state::term_structure_table&
    term_structures() const noexcept {
        return term_structures_;
    }

    /** @brief The bounded cache of tenant images by identifier. */
    [[nodiscard]] model::state::image_cache& images() noexcept {
        return images_;
    }
    [[nodiscard]] const model::state::image_cache& images() const noexcept {
        return images_;
    }

    /** @brief Images whose reads failed, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    failed_images() const noexcept {
        return failed_images_;
    }

    /** @brief Images whose reads are in flight, keyed as the cache keys them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    pending_images() const noexcept {
        return pending_.subjects(request_kind::image);
    }

    /**
     * @brief Starts the read a window asked for.
     *
     * Refuses only when a window's read needs the store and it is not open.
     * Every other refusal is silent: a window over a session that cannot read
     * draws its own session-loss state.
     */
    [[nodiscard]] std::optional<refusal> start(const read_request& request);

    /** @brief Reads a tenant's currency-to-image map. */
    void start_currency_image_map(const model::event::event& ev);

    /**
     * @brief Reads the currency image map again when the first read failed.
     *
     * A refresh is the operator's chance to retry it. Flags fall back to the
     * drawn ones until it answers.
     */
    void retry_currency_image_map(std::string_view instance_id,
        std::string_view account_id);

    /** @brief Reads one tenant image by identifier for a session. */
    void load_image(const model::command::command& cmd);

    /** @brief Reads the latest curve named by a set of tenor series. */
    void load_term_structure(const model::command::command& cmd);

    /** @brief Reads the CRM names a party offers. */
    void load_crm_names(const model::command::command& cmd);

    /** @brief Asks for the market series catalogue, on the window's session. */
    [[nodiscard]] std::optional<refusal>
    start_market_catalogue_read(std::string_view window_id);

    /** @brief The same, for the session signed in as an instance and account. */
    [[nodiscard]] std::optional<refusal> start_market_catalogue_read(
        std::string_view instance_id, std::string_view account_id);

    /**
     * @brief Whether a catalogue read for the pair is already in flight.
     *
     * A party change must read the catalogue again, but a read that started
     * under the old party is holding the pending mark. The caller waits for
     * that answer and then starts the new read, rather than passing a request
     * that would be dropped.
     */
    [[nodiscard]] bool market_catalogue_pending(std::string_view instance_id,
        std::string_view account_id) const;

    /** @brief Seeds one watched series' history on a fresh connection. */
    void start_market_backfill(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id);

    /**
     * @brief Reads older observations when a chart span needs them.
     *
     * The command names a series and a span. Nothing is read when the series'
     * level already holds the span, when an earlier read already asked that far
     * back, when a history read for the series is in flight, or when no session
     * can read. Frame thread only; the answer is seeded by @c apply.
     */
    void load_history(const model::command::command& cmd);

    /** @brief History reads started since the service was made. */
    [[nodiscard]] std::size_t history_reads_started() const noexcept {
        return history_reads_started_;
    }

    /**
     * @brief Reads each CRM batch an open cross-rates window shows, when due.
     *
     * The deadline belongs to the batch rather than the window, so any number
     * of windows over one matrix cost one read per cadence. Whether a window is
     * collapsed or drawn plays no part, because the interface never asks.
     */
    void pump_crm_reads();

    /**
     * @brief Clears a read's in-flight mark and applies its answer.
     *
     * @return true when the event was wholly a read answer, false when the
     * session row still has something to apply.
     */
    [[nodiscard]] bool apply(const model::event::event& ev);

    /** @brief The reads in flight, for a test to see a mark come and go. */
    [[nodiscard]] const pending_reads& pending() const noexcept {
        return pending_;
    }

    /** @brief Forgets every table and every mark. */
    void clear();

private:
    /** @brief What a read needs of a session before it may start. */
    enum class read_gate {
        signed_in,
        signed_in_with_token,
        token_only,
    };

    /** @brief The session a read runs on: which deployment, and as whom. */
    struct read_session final {
        std::string instance_id;
        std::string account_id;
        std::string tenant_id;
    };

    /**
     * @brief The session a read on @p session would run on.
     *
     * Empty when the session fails @p gate. Every refusal is silent: a window
     * over a session that cannot read draws its own session-loss state.
     */
    [[nodiscard]] std::optional<read_session> session_read_for(
        model::state::session_handle session, read_gate gate) const;

    /** @brief The same, for the session signed in as one instance and account. */
    [[nodiscard]] std::optional<read_session> session_read_for(
        std::string_view instance_id, std::string_view account_id,
        read_gate gate) const;

    /**
     * @brief Hands @p request to the session's actor.
     *
     * False when there is no actor to run it, and then nothing is in flight:
     * the read's mark is set only after this says true, so a read that could
     * not be started is not left marked.
     */
    [[nodiscard]] bool submit_read(const read_session& read,
        worker::read_request request);

    [[nodiscard]] bool is_open() const noexcept {
        return !database_path_.empty();
    }

    /**
     * @brief Asks for one command centre's deployment read.
     *
     * One read at a time per session. A read already in flight means the
     * cadence asked again before the deployment answered, and a second read
     * would queue behind the first for no new information.
     */
    [[nodiscard]] std::optional<refusal>
    start_deployment_read(std::string_view window_id);

    /**
     * @brief Asks for one market read on the window's session.
     *
     * An empty @p series_id is the catalogue; anything else is that series'
     * points. One path because the connection, token and in-flight rules are
     * the same for both.
     */
    [[nodiscard]] std::optional<refusal>
    start_market_read(std::string_view window_id, std::string_view series_id);

    /** @brief Starts a catalogue read (empty series) or a points read. */
    [[nodiscard]] std::optional<refusal> launch_market_read(
        read_session read, std::string_view series_id);

    /** @brief Asks for one series' points, on the window's session. */
    [[nodiscard]] std::optional<refusal>
    start_market_points_read(std::string_view window_id,
        std::string_view series_id);

    /** @brief Reads the curve a term-structure window shows. */
    void start_term_structure_read(model::state::window_handle handle);

    /** @brief Reads the CRM batch a cross-rates window shows. */
    void start_crm_read(model::state::window_handle handle);

    void start_crm_names_read(std::string_view instance_id,
        std::string_view account_id, std::string_view party_id);

    const model::state::session_table& sessions_;
    const model::state::window_table& windows_;
    read_submit submit_;
    std::filesystem::path database_path_;

    /**
     * @brief Every read in flight.
     *
     * A read is short-lived and has no cancellation of its own, so a mark is
     * all the state it needs: it is set when the read starts and cleared by
     * whatever answer ends it.
     */
    pending_reads pending_;

    /** @brief Tenant-and-image whose read failed, so it is not asked again. */
    std::unordered_set<std::string> failed_images_;

    /** @brief The market series, their catalogues and the points each holds. */
    model::state::series_table& market_;

    /** @brief Points answers dropped for a series nobody holds. */
    std::size_t unheld_points_ = 0;

    /** @brief History reads handed to the pool. */
    std::size_t history_reads_started_ = 0;

    /** @brief Which image each tenant currency names. */
    model::state::tenant_image_map currency_images_;

    /** @brief The bounded cache of tenant images by identifier. */
    model::state::image_cache images_;

    /** @brief The latest curve each browser session read. */
    model::state::term_structure_table term_structures_;

    model::state::crm_table crm_;
};

}

#endif
