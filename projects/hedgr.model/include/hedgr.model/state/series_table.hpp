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
#ifndef HEDGR_MODEL_STATE_SERIES_TABLE_HPP
#define HEDGR_MODEL_STATE_SERIES_TABLE_HPP

#include "hedgr.model/domain/market.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/composite_key.hpp"
#include "hedgr.model/state/handle.hpp"
#include "hedgr.model/state/series_ring.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/** @brief The default resident budget: 256 MiB of ring buffers. */
inline constexpr std::size_t series_default_budget = 256u * 1024u * 1024u;

/** @brief Where one session's series catalogue read stands. */
enum class catalogue_state : std::uint8_t {
    /** @brief Nothing has been asked for. */
    unread = 0,
    /** @brief A read is in flight. */
    reading = 1,
    /** @brief The last read succeeded. */
    ready = 2,
    /** @brief The last read failed; the error says why. */
    failed = 3,
};

/** @brief What the table did with an appended bucket. */
enum class append_result : std::uint8_t {
    /** @brief The bucket was added or replaced the tail. */
    applied = 0,
    /** @brief The bucket was older than the level's newest and was dropped. */
    late = 1,
    /** @brief The handle named a row that is gone, and nothing changed. */
    stale = 2,
};

/** @brief The cold description of a series row, from the catalogue. */
struct series_info final {
    /** @brief The canonical series URI, which routes ticks, or empty. */
    std::string uri;

    /** @brief The party that owns the series, or empty. */
    std::string party_id;

    /** @brief The tenor the series carries, or empty. */
    std::string tenor;

    /** @brief The curve the series belongs to, or empty. */
    std::string curve_key;

    /** @brief Whether the session's catalogue lists the series now. */
    bool listed = false;
};

/**
 * @brief What a row knows of the deeper reads made for its chart spans.
 *
 * The first backfill reads the newest observations only. A longer span asks for
 * more, one read at a time, and this is the record that stops the same ask
 * being made again.
 */
struct series_history final {
    /** @brief Whether a deeper read is in flight for the row. */
    bool reading = false;

    /**
     * @brief How far back the deployment was asked, or zero for never.
     *
     * An epoch instant. A read that failed leaves it alone, so the next
     * explicit ask retries.
     */
    std::int64_t floor_ns = 0;

    /** @brief Whether the last answer stopped at the read's bound. */
    bool truncated = false;
};

/** @brief The three names that identify a series, borrowed from its row. */
struct series_key final {
    std::string_view instance_id;
    std::string_view account_id;
    std::string_view series_id;
};

/** @brief Hashes a series id or a borrowed view of one alike. */
struct series_id_hash final {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view id) const noexcept {
        return std::hash<std::string_view>{}(id);
    }
};

/**
 * @brief The catalogue one session read, and the generation it is at.
 *
 * The rows are the chooser's order. The index finds a row by series id so a
 * refresh of the held series does not scan the catalogue.
 */
struct HEDGR_MODEL_EXPORT series_catalogue final {
    /** @brief The series, in the order the chooser shows them. */
    std::vector<domain::market_series_row> series;

    /** @brief Where the read stands. */
    catalogue_state state = catalogue_state::unread;

    /** @brief Why the last read failed, or empty. */
    std::string error;

    /**
     * @brief Changes whenever the catalogue is begun, replaced or failed.
     *
     * Drawn from the table's one counter, so it never repeats. A view derived
     * from the catalogue is current while it matches.
     */
    std::uint64_t generation = 0;

    /** @brief The row of a series id, or nullptr. */
    [[nodiscard]] const domain::market_series_row*
    row_of(std::string_view series_id) const;

    /** @brief Series id to position in @c series. */
    std::unordered_map<std::string, std::size_t, series_id_hash,
        std::equal_to<>> index;
};

/**
 * @brief The market series each session holds: catalogues, rows and rings.
 *
 * A catalogue is kept per session scope, an instance and an account. A series
 * row is made by intern, not by a catalogue arriving, so a chart restored
 * before its session signs in already has its handle. A catalogue refreshes the
 * cold description of the rows that exist and never touches their rings.
 *
 * Each row owns three rings, one per level, and one version stamp. The stamp
 * comes from one counter shared by every row and every catalogue, so no stamp
 * is reused by another series and none repeats after clear. A derived value
 * over several rows is current while the largest of their stamps is unchanged.
 *
 * Memory is bounded three ways. A ring keeps its level's capacity. A row that
 * nobody watches is evicted, least recently released first, when the rings
 * hold more than the budget; a watched row is never evicted. forget_scope
 * removes a whole session. Evicting or removing a row releases its slot, so a
 * handle to it is rejected from then on.
 *
 * Frame thread only, with the rest of the tables.
 */
class HEDGR_MODEL_EXPORT series_table final {
public:
    /** @brief A table that evicts unwatched rows above @p budget_bytes. */
    explicit series_table(std::size_t budget_bytes = series_default_budget);

    /** @brief Records one session's catalogue and refreshes its held rows. */
    void apply_catalogue(std::string_view instance_id,
        std::string_view account_id,
        std::vector<domain::market_series_row> rows);

    /** @brief Marks one session's catalogue read as started. */
    void begin_catalogue(std::string_view instance_id,
        std::string_view account_id);

    /** @brief Records a failed catalogue read for one session. */
    void fail_catalogue(std::string_view instance_id,
        std::string_view account_id, std::string message);

    /** @brief One session's catalogue, or nullptr when it has none. */
    [[nodiscard]] const series_catalogue*
    catalogue(std::string_view instance_id, std::string_view account_id) const;

    /**
     * @brief The row of a series, made if it does not exist.
     *
     * Interning a held series returns its handle again. A new row takes its
     * description from the session's catalogue when the catalogue lists the
     * series. A new row has no watchers and counts as just released.
     */
    [[nodiscard]] series_handle intern(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id);

    /** @brief The row of a series, or nothing. Allocates nothing. */
    [[nodiscard]] std::optional<series_handle>
    find(std::string_view instance_id, std::string_view account_id,
        std::string_view series_id) const;

    /** @brief Whether @p h names a row that is still held. */
    [[nodiscard]] bool contains(series_handle h) const noexcept;

    /**
     * @brief Adds a watcher to a row. A stale handle is ignored.
     *
     * A row going from none to one watcher is recorded as a change.
     */
    void acquire(series_handle h);

    /**
     * @brief Removes a watcher from a row.
     *
     * When the count reaches zero the row counts as just released and is
     * recorded as a change, and unwatched rows are evicted while the rings
     * hold more than the budget.
     */
    void release(series_handle h);

    /**
     * @brief Hands over the rows whose watch state may have flipped.
     *
     * Replaces the contents of @p out with every row that gained its first
     * watcher, lost its last, was erased while watched, or whose catalogue
     * description changed while watched, since the last call. A handle can
     * appear more than once and may be stale by now, so a reader checks the
     * row as it stands. Allocates nothing when no row changed.
     */
    void drain_changes(std::vector<series_handle>& out);

    /** @brief The rows of one session, in the order they were interned. */
    [[nodiscard]] std::span<const series_handle>
    held(std::string_view instance_id, std::string_view account_id) const;

    /** @brief The names of a row, or nothing for a stale handle. */
    [[nodiscard]] std::optional<series_key> key_of(series_handle h) const noexcept;

    /** @brief The watchers of every row added up, for an audit of the holders. */
    [[nodiscard]] std::size_t watcher_total() const noexcept;

    /** @brief Removes a row now, whoever watches it. Its handle goes stale. */
    void erase(series_handle h);

    /**
     * @brief The six columns of one level.
     *
     * Empty for a stale handle or a level with no points. Allocates nothing.
     */
    [[nodiscard]] series_columns columns(series_handle h,
        domain::market_level level) const noexcept;

    /**
     * @brief Adds one closed bucket to a level of a row.
     *
     * Stamps the row only when the bucket is applied. A late bucket changes
     * nothing a plot shows, so it leaves the stamp; it still counts as a sign
     * of life, so it records @p observed_at_ns and clears a failure, and the
     * failure's removal stamps the row.
     */
    append_result append(series_handle h, domain::market_level level,
        const series_point& point, std::int64_t observed_at_ns);

    /**
     * @brief Seeds a row from a backfill read.
     *
     * The observations are collapsed by bucket at each level, the last one in
     * a bucket winning, and merged into the rings by bucket id with the held
     * point winning. Stamps the row when anything changed.
     *
     * @return whether anything changed. False for a stale handle.
     */
    bool seed(series_handle h, std::span<const domain::market_point> points,
        std::int64_t observed_at_ns);

    /** @brief Marks a deeper read as started. A stale handle is ignored. */
    void begin_history(series_handle h);

    /**
     * @brief Records that a deeper read for spans reaching back to @p from_ns
     * answered, and whether it stopped at its bound.
     *
     * The floor only moves back, so a shallower answer never forgets a deeper
     * ask. Does not stamp the row: the seed of the points does when they change
     * what a plot shows.
     */
    void finish_history(series_handle h, std::int64_t from_ns, bool truncated);

    /** @brief Marks a deeper read as ended without an answer worth keeping. */
    void abandon_history(series_handle h);

    /** @brief The row's deeper-read record, empty for a stale handle. */
    [[nodiscard]] series_history history(series_handle h) const noexcept;

    /** @brief Records a failed points read and stamps the row. */
    void fail(series_handle h, std::string message,
        std::int64_t observed_at_ns);

    /** @brief The row's stamp, or zero for a stale handle. */
    [[nodiscard]] std::uint64_t version(series_handle h) const noexcept;

    /** @brief Why the last read failed, or empty. */
    [[nodiscard]] std::string_view failure(series_handle h) const noexcept;

    /** @brief When the last read or tick arrived, on the monotonic clock. */
    [[nodiscard]] std::int64_t observed_at_ns(series_handle h) const noexcept;

    /** @brief The newest point of any level, or zero. */
    [[nodiscard]] std::int64_t newest_at_ns(series_handle h) const noexcept;

    /** @brief The row's catalogue description, or nullptr when stale. */
    [[nodiscard]] const series_info* info(series_handle h) const noexcept;

    /** @brief The row's watchers, or zero when stale. */
    [[nodiscard]] std::uint32_t watchers(series_handle h) const noexcept;

    /**
     * @brief Removes every row and the catalogue of one session.
     * @return the number of rows removed.
     */
    std::size_t forget_scope(std::string_view instance_id,
        std::string_view account_id);

    /**
     * @brief Removes every row and catalogue of one instance, all accounts.
     * @return the number of rows removed.
     */
    std::size_t forget_instance(std::string_view instance_id);

    /** @brief Removes every row and every catalogue. Stamps keep rising. */
    void clear();

    /** @brief Changes the budget and evicts unwatched rows down to it. */
    void set_budget(std::size_t budget_bytes);

    /** @brief The bytes the rings hold. */
    [[nodiscard]] std::size_t resident_bytes() const noexcept {
        return resident_;
    }

    /** @brief The number of rows held. */
    [[nodiscard]] std::size_t size() const noexcept {
        return layout_.live_count;
    }

private:
    struct row final {
        std::string instance_id;
        std::string account_id;
        std::string series_id;
        series_info info;
        std::array<series_ring, 3> rings{
            series_ring{domain::market_fine_capacity},
            series_ring{domain::market_medium_capacity},
            series_ring{domain::market_coarse_capacity}};
        std::string failure;
        series_history history;
        std::int64_t observed_at_ns = 0;
        std::uint64_t stamp = 0;
        std::uint64_t released_at = 0;
        std::uint32_t watchers = 0;
    };

    struct scope final {
        series_catalogue catalogue;
        std::vector<series_handle> held;
    };

    [[nodiscard]] row* row_of(series_handle h) noexcept;
    [[nodiscard]] const row* row_of(series_handle h) const noexcept;
    [[nodiscard]] std::size_t ring_bytes(const row& r) const noexcept;
    bool refresh(row& r, const series_catalogue& catalogue);
    void evict_to_budget();

    composite_map<2, scope> scopes_;
    composite_map<3, series_handle> keys_;
    slab layout_;
    std::vector<row> rows_;
    std::size_t budget_;
    std::size_t resident_ = 0;
    /** @brief The last stamp issued; never reset, so none repeats. */
    std::uint64_t counter_ = 0;
    std::vector<series_handle> changes_;
};

}

#endif
