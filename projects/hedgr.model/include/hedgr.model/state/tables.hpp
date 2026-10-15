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
#ifndef HEDGR_MODEL_STATE_TABLES_HPP
#define HEDGR_MODEL_STATE_TABLES_HPP

#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/handle.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief The instances known to this process, addressed by handle.
 *
 * Instances are cold: tens of rows, read when the Instance Manager is drawn and
 * never touched by the frame loop's per-window path. Splitting them into
 * parallel columns would buy nothing, so the table keeps the value whole and
 * keys it by the persisted id.
 *
 * The rows sit in a sparse slab rather than a compacting vector, because a
 * probe result and, later, a label junction address an instance by handle and
 * must keep addressing the same instance after a removal. Both @c rows() and
 * @c layout() include the holes a removal leaves behind, so a whole-table walk
 * checks @c layout().is_alive(index) before reading a slot.
 */
class HEDGR_MODEL_EXPORT instance_table final {
public:
    /**
     * @brief Adds @p row, or replaces the row carrying the same id.
     *
     * A replacement keeps the handle the previous row held, so a caller that
     * already resolved this instance keeps addressing it.
     */
    [[nodiscard]] instance_handle upsert(domain::instance row);

    void remove(instance_handle h);

    [[nodiscard]] bool contains(instance_handle h) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    /** @brief The handle carrying this persisted id, when there is one. */
    [[nodiscard]] std::optional<instance_handle>
    find(std::string_view id) const;

    /**
     * @brief The row a handle addresses, or nullptr when the handle is stale.
     *
     * A caller at a boundary turns a string id into a handle with find() and
     * asks either contains() or this. A caller holding a handle it just
     * received needs neither check.
     */
    [[nodiscard]] const domain::instance* get(instance_handle h) const noexcept;
    [[nodiscard]] domain::instance* get(instance_handle h) noexcept;

    /** @brief Aliveness and generations, holes included. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief Every row, holes included, for a whole-table walk. */
    [[nodiscard]] std::span<const domain::instance> rows() const noexcept {
        return rows_;
    }

    void clear() noexcept;

private:
    slab layout_;
    std::vector<domain::instance> rows_;
    std::unordered_map<std::string, instance_handle> by_id_;
};

/**
 * @brief The accounts known to this process, addressed by handle.
 *
 * Cold, like the instance table, and shaped the same way: a sparse slab, a map
 * from the persisted id, and @c rows() and @c layout() both including the holes
 * a removal leaves behind, so a whole-table walk checks
 * @c layout().is_alive(index).
 */
class HEDGR_MODEL_EXPORT account_table final {
public:
    /**
     * @brief Adds @p row, or replaces the row carrying the same id.
     *
     * A replacement keeps the handle the previous row held.
     */
    [[nodiscard]] account_handle upsert(domain::account row);

    void remove(account_handle h);

    [[nodiscard]] bool contains(account_handle h) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    /** @brief The handle carrying this persisted id, when there is one. */
    [[nodiscard]] std::optional<account_handle> find(std::string_view id) const;

    /** @brief The row a handle addresses, or nullptr when it is stale. */
    [[nodiscard]] const domain::account* get(account_handle h) const noexcept;
    [[nodiscard]] domain::account* get(account_handle h) noexcept;

    /** @brief The accounts belonging to one instance, holes skipped. */
    [[nodiscard]] std::vector<const domain::account*>
    children_of(std::string_view instance_id) const;

    /** @brief Aliveness and generations, holes included. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief Every row, holes included, for a whole-table walk. */
    [[nodiscard]] std::span<const domain::account> rows() const noexcept {
        return rows_;
    }

    void clear() noexcept;

private:
    slab layout_;
    std::vector<domain::account> rows_;
    std::unordered_map<std::string, account_handle> by_id_;
};

/**
 * @brief The live columns of a session, split by how often they are read.
 *
 * The frame loop reads status and latency for the connection indicator, so they
 * sit in the first block. The tenant, party and last error are read only when a
 * detail surface opens, so they sit last and never enter the per-frame walk.
 */
struct session_columns final {
    // Hot: read by the frame loop.
    std::vector<std::uint8_t> status;
    std::vector<std::int64_t> latency_us;

    // Warm: read when a session row is drawn.
    std::vector<std::string> instance_id;
    std::vector<std::string> account_id;

    // Cold: read only when a detail surface opens.
    std::vector<std::string> last_error;
    std::vector<std::string> tenant_name;
    std::vector<std::string> party_name;
    std::vector<std::string> token;
};

/**
 * @brief Live sessions, keyed by handle and looked up by (instance, account).
 *
 * Every accessor takes a handle and has the precondition that the handle is
 * live. Callers at a boundary check contains() first; callers holding a handle
 * they just received do not.
 */
class HEDGR_MODEL_EXPORT session_table final {
public:
    /** @brief Creates a disconnected session for the pair. */
    [[nodiscard]] session_handle
    insert(std::string instance_id, std::string account_id);

    void remove(session_handle h);

    [[nodiscard]] bool contains(session_handle h) const noexcept {
        return owns(layout_, h);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return layout_.live_count;
    }

    /** @brief The handle for a pair, or nothing when no session exists. */
    [[nodiscard]] std::optional<session_handle>
    find(std::string_view instance_id, std::string_view account_id) const;

    [[nodiscard]] domain::session_status status(session_handle h) const;
    void set_status(session_handle h, domain::session_status value);

    [[nodiscard]] std::int64_t latency_us(session_handle h) const;
    void set_latency_us(session_handle h, std::int64_t value);

    [[nodiscard]] std::string_view last_error(session_handle h) const;
    void set_last_error(session_handle h, std::string value);

    /** @brief Records what IAM returned once the account was accepted. */
    void set_account_result(session_handle h, std::string tenant_name,
                             std::string party_name, std::string token);

    [[nodiscard]] std::string_view instance_id(session_handle h) const;
    [[nodiscard]] std::string_view account_id(session_handle h) const;
    [[nodiscard]] std::string_view tenant_name(session_handle h) const;
    [[nodiscard]] std::string_view party_name(session_handle h) const;

    /** @brief Aliveness and generations, for a handle-walking loop. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief The status column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::uint8_t> status_column() const noexcept {
        return columns_.status;
    }

    /** @brief The latency column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::int64_t> latency_us_column() const noexcept {
        return columns_.latency_us;
    }

    void clear() noexcept;

private:
    session_columns columns_;
    slab layout_;
    std::unordered_map<std::string, session_handle> by_key_;
};

/**
 * @brief The live columns of a window, split by how often they are read.
 *
 * The frame loop reads kind, open and the refresh deadline for every window on
 * every frame, to decide what to draw and what is due. The id, the cadence and
 * the state payload are read only for a window that survived both gates, so
 * they sit in the cold block and the frame loop never touches them for a culled
 * window.
 */
struct window_columns final {
    // Hot: read by the frame loop for culling and dispatch.
    std::vector<std::uint8_t> kind;
    std::vector<std::uint8_t> open;
    std::vector<std::uint32_t> session_index;
    std::vector<std::uint32_t> session_generation;
    std::vector<std::int64_t> next_refresh_ns;

    // Cold: read only for an open window whose deadline has passed.
    std::vector<std::string> id;
    std::vector<std::int64_t> cadence_ms;
    std::vector<domain::window_state> state;
};

/**
 * @brief Live windows, keyed by handle and looked up by stable id.
 */
class HEDGR_MODEL_EXPORT window_table final {
public:
    /** @brief Creates a window from its persisted description. */
    [[nodiscard]] window_handle insert(const domain::window_spec& spec);

    void remove(window_handle h);

    [[nodiscard]] bool contains(window_handle h) const noexcept {
        return owns(layout_, h);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return layout_.live_count;
    }

    /** @brief The handle with this stable id, or nothing. */
    [[nodiscard]] std::optional<window_handle> find(std::string_view id) const;

    [[nodiscard]] bool is_open(window_handle h) const;
    void set_open(window_handle h, bool value);

    [[nodiscard]] domain::window_kind kind(window_handle h) const;

    [[nodiscard]] std::string_view id(window_handle h) const;

    [[nodiscard]] const domain::window_state& state(window_handle h) const;
    [[nodiscard]] domain::window_state& state(window_handle h);

    [[nodiscard]] std::int64_t next_refresh_ns(window_handle h) const;
    void set_next_refresh_ns(window_handle h, std::int64_t value);

    /**
     * @brief Binds a window to the session that owns it.
     *
     * The window stores the handle, not the key, so the caller resolves the
     * session first. The pair of columns stays stride-1, which keeps
     * session_of() off the map lookup path.
     */
    void bind_session(window_handle h, session_handle session);
    void clear_session(window_handle h);

    [[nodiscard]] std::optional<session_handle> session_of(window_handle h) const;

    /**
     * @brief The window's persisted description.
     *
     * The session key is resolved through the session table because a window
     * stores a handle, not the key. This is the cold path, taken when the store
     * saves, so the extra lookup costs nothing on the frame loop.
     */
    [[nodiscard]] domain::window_spec
    spec(window_handle h, const session_table& sessions) const;

    /** @brief Every window's description, for the store. */
    [[nodiscard]] std::vector<domain::window_spec>
    specs(const session_table& sessions) const;

    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief The kind column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::uint8_t> kind_column() const noexcept {
        return columns_.kind;
    }

    /** @brief The open column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::uint8_t> open_column() const noexcept {
        return columns_.open;
    }

    /** @brief The refresh deadline column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::int64_t> next_refresh_column() const noexcept {
        return columns_.next_refresh_ns;
    }

    void clear() noexcept;

private:
    window_columns columns_;
    slab layout_;
    std::unordered_map<std::string, window_handle> by_id_;
};

}

#endif
