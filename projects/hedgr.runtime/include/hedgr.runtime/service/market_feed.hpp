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
#ifndef HEDGR_RUNTIME_SERVICE_MARKET_FEED_HPP
#define HEDGR_RUNTIME_SERVICE_MARKET_FEED_HPP

#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_table.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/refusal.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::runtime::service {

class session_service;

/**
 * @brief The live price feed of every authenticated session.
 *
 * One stream per session, in a map keyed by the session's running key. The
 * stream owns the connection and its thread. This class owns the watch set,
 * asks for the catalogue and the backfill, and drains closed buckets into the
 * series table. It runs on the frame thread and is the only writer of the
 * buckets it applies. The NATS dispatch thread and the stream thread never
 * reach a member of it.
 *
 * The windows hold the series they show, and the table counts those holders.
 * A series is wanted when it has a holder, its catalogue lists it, and it is
 * the session's party's. The feed reads the table's change list for the rows
 * whose state may have flipped and does a full pass over a session's held rows
 * only when its stream opens, so a desk where nothing changed costs it the
 * drain and nothing else.
 *
 * Supervision decides when a stream exists and calls @c open, @c retoken_or_open
 * and @c close. This class never reads a session's status to decide that
 * except in @c pump, where a session that is no longer authenticated loses its
 * stream whatever supervision did.
 *
 * Holds no reference to the manager. The tables it reads are passed in as
 * const references, the series table it appends to as a reference, the actors
 * it plots on as the session service, and the two reads it asks for as
 * callbacks, so it never reaches the read service.
 */
class HEDGR_RUNTIME_EXPORT market_feed final {
public:
    /** @brief Starts one session's catalogue read, or refuses. */
    using catalogue_read = std::function<std::optional<refusal>(
        std::string_view instance_id, std::string_view account_id)>;

    /** @brief Starts the backfill read of one series. */
    using backfill_read = std::function<void(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id)>;

    /**
     * @param actors the session's actors, which this feed plots on and does
     * not own.
     */
    market_feed(const model::state::instance_table& instances,
        const model::state::session_table& sessions,
        model::state::series_table& series,
        const session::transport::session_options& seed,
        session_service& actors, catalogue_read read_catalogue,
        backfill_read read_backfill);

    /** @brief Records where the store is, which a stream's options need. */
    void attach(std::filesystem::path database_path);

    /** @brief Opens the live stream for one authenticated session. */
    void open(const model::event::event& ev);

    /**
     * @brief Hands a refreshed token to the session's stream, or opens one.
     *
     * A refresh replaces the credential and nothing else, so a stream that
     * exists keeps its subscription and its open buckets.
     */
    void retoken_or_open(const model::event::event& ev);

    /** @brief Stops and forgets one session's live stream. */
    void close(std::string_view instance_id, std::string_view account_id);

    /**
     * @brief Seeds every series the session watches again.
     *
     * Called when the session's connection answers after an outage, because the
     * ticks published while it was down are gone. A session with no stream, or
     * a series the table no longer names, costs nothing.
     */
    void link_restored(
        std::string_view instance_id, std::string_view account_id);

    /**
     * @brief Applies the watch changes, drains buckets and applies them.
     * @return the catalogue reads that were refused, for the facade to raise.
     */
    [[nodiscard]] std::vector<refusal> pump();

    /** @brief Asks every stream to stop and forgets them all. */
    void cancel();

    /**
     * @brief Buckets dropped because their series was released in flight.
     *
     * The handle's generation no longer matched, so writing the bucket would
     * have put it in the series that now holds the slot.
     */
    [[nodiscard]] std::size_t stale_buckets() const noexcept {
        return stale_buckets_;
    }

    /** @brief Buckets dropped because they were older than the newest held. */
    [[nodiscard]] std::size_t late_buckets() const noexcept {
        return late_buckets_;
    }

private:
    const model::state::instance_table& instances_;
    const model::state::session_table& sessions_;
    model::state::series_table& series_;
    const session::transport::session_options& seed_;

    /** @brief The actors this feed plots on. It does not own them. */
    session_service& actors_;

    catalogue_read read_catalogue_;
    backfill_read read_backfill_;
    std::filesystem::path database_path_;

    [[nodiscard]] bool is_open() const noexcept {
        return !database_path_.empty();
    }

    /**
     * @brief One authenticated session's live prices.
     *
     * The actor owns the connection and its thread; the feed owns the watch
     * set, the backfill and the drain into the model. Keyed by the session's
     * running key. The actor is held here because the feed asks it for
     * buckets every frame, and the session service holds it too, so a retired
     * actor stays alive until its thread has finished.
     */
    struct market_stream_entry final {
        std::string instance_id;
        std::string account_id;
        std::shared_ptr<worker::tick_feed> stream;

        /**
         * @brief The session the stream was opened for.
         *
         * Held as a handle because the frame checks it every pump, and a
         * handle check allocates nothing where a lookup by name builds a key.
         * A session that was removed leaves the handle stale.
         */
        model::state::session_handle session;

        /** @brief The series the stream is told to watch. */
        std::vector<model::state::series_handle> watched;

        /** @brief Set when the stream opens, until its full pass has run. */
        bool needs_pass = true;
    };

    /**
     * @brief Makes the stream watch exactly the series this session wants.
     *
     * Idempotent. Builds the whole wanted set and hands it to the stream once,
     * so the stream applies a level rather than a sequence of edits. Starts
     * the session's catalogue read when it has none, and the refusal of that
     * read is added to @p refusals.
     */
    void reconcile(market_stream_entry& entry,
        std::vector<refusal>& refusals);

    /** @brief Whether @p series should be watched on @p entry's stream. */
    [[nodiscard]] bool wanted(const market_stream_entry& entry,
        model::state::series_handle series, std::vector<refusal>& refusals);

    /** @brief Drops @p series from every stream that watches it. */
    void forget(model::state::series_handle series);

    /** @brief Reused every frame, so an idle desk allocates nothing. */
    std::vector<model::state::series_handle> changes_;
    std::vector<model::state::series_bucket> buckets_;

    std::size_t stale_buckets_ = 0;
    std::size_t late_buckets_ = 0;

    std::unordered_map<std::string, market_stream_entry> market_streams_;
};

}

#endif
