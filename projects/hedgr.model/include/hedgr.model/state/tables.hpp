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
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/domain/deployment.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.model/export.hpp"
#include "hedgr.model/state/handle.hpp"
#include <cstdint>
#include <expected>
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

    /**
     * @brief Puts the instance in @p folder_id, and nowhere else.
     *
     * The one column of an instance row the runtime moves on its own, when a
     * folder is removed and its contents are re-homed. A general mutable
     * accessor would be a second writer beside upsert(); this is the single
     * field that has a reason to change without the rest of the row being
     * rewritten from a caller's copy.
     */
    void set_folder(instance_handle h, std::string folder_id);

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

    /** @brief Puts the account in @p folder_id. See instance_table::set_folder. */
    void set_folder(account_handle h, std::string folder_id);

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
 * @brief The folders of every tree, addressed by handle.
 *
 * Cold, like the instance table, and shaped the same way: a sparse slab, a map
 * from the persisted id, and @c rows() and @c layout() both including the holes
 * a removal leaves behind.
 *
 * The interface walks folders parent by parent rather than id by id, so
 * @c children_of and @c root scan the slab instead of consulting an index. The
 * tables are tens of rows, so a scan is cheaper than the bookkeeping a
 * parent-to-children map would need on every removal.
 */
class HEDGR_MODEL_EXPORT folder_table final {
public:
    /**
     * @brief Adds @p row, or replaces the row carrying the same id.
     *
     * Refuses @p row when it would make the folder its own ancestor, because
     * the tree is walked by following @c parent_id upwards and a cycle would
     * make that walk never terminate. The refusal is returned rather than
     * silently repaired, so the caller learns that the move it asked for was
     * not applied.
     */
    [[nodiscard]] std::expected<folder_handle, std::string>
    upsert(domain::folder row);

    void remove(folder_handle h);

    [[nodiscard]] bool contains(folder_handle h) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    /** @brief The handle carrying this persisted id, when there is one. */
    [[nodiscard]] std::optional<folder_handle> find(std::string_view id) const;

    /** @brief The row a handle addresses, or nullptr when it is stale. */
    [[nodiscard]] const domain::folder* get(folder_handle h) const noexcept;
    [[nodiscard]] domain::folder* get(folder_handle h) noexcept;

    /** @brief The folders whose parent is @p parent_id, holes skipped. */
    [[nodiscard]] std::vector<const domain::folder*>
    children_of(std::string_view parent_id) const;

    /**
     * @brief The root of one tree owned by one owner.
     *
     * A root is the folder with no parent. The instances tree has one ownerless
     * root; each instance owns its own accounts root. The owner is part of the
     * question rather than an afterthought, because that is what makes the
     * answer unique.
     */
    [[nodiscard]] std::optional<folder_handle>
    root(domain::folder_tree tree, std::string_view owner_id) const;

    /**
     * @brief Every folder below @p folder_id, depth first, excluding it.
     *
     * This is the list a cascade removes, and the list a reparent must not name
     * as a new parent.
     */
    [[nodiscard]] std::vector<std::string>
    descendants_of(std::string_view folder_id) const;

    /**
     * @brief Every folder above @p folder_id, nearest first, excluding it.
     *
     * This is what a containment check walks: a folder contains another exactly
     * when the other's ancestors include it.
     */
    [[nodiscard]] std::vector<std::string>
    ancestors_of(std::string_view folder_id) const;

    /** @brief Aliveness and generations, holes included. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief Every row, holes included, for a whole-table walk. */
    [[nodiscard]] std::span<const domain::folder> rows() const noexcept {
        return rows_;
    }

    void clear() noexcept;

private:
    /** @brief Whether @p candidate lies on the parent chain of @p id. */
    [[nodiscard]] bool is_ancestor_of(std::string_view candidate,
                                      std::string_view id) const;

    slab layout_;
    std::vector<domain::folder> rows_;
    std::unordered_map<std::string, folder_handle> by_id_;
};

/**
 * @brief The labels known to this process, addressed by handle.
 *
 * Cold, and shaped like the instance table. It is keyed by name as well as by
 * id because a label is written by a person and read back by the name typed
 * into a filter, while the junction that attaches it speaks ids. Two indexes
 * over a table of tens of rows is cheaper than making either caller translate.
 */
class HEDGR_MODEL_EXPORT label_table final {
public:
    /**
     * @brief Adds @p row, or replaces the row carrying the same id.
     *
     * A replacement keeps the handle the previous row held, so a junction that
     * already resolved this label keeps addressing it.
     */
    [[nodiscard]] label_handle upsert(domain::label row);

    void remove(label_handle h);

    [[nodiscard]] bool contains(label_handle h) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    /** @brief The handle carrying this persisted id, when there is one. */
    [[nodiscard]] std::optional<label_handle> find(std::string_view id) const;

    /** @brief The handle carrying this name, when there is one. */
    [[nodiscard]] std::optional<label_handle>
    find_by_name(std::string_view name) const;

    /** @brief The row a handle addresses, or nullptr when it is stale. */
    [[nodiscard]] const domain::label* get(label_handle h) const noexcept;
    [[nodiscard]] domain::label* get(label_handle h) noexcept;

    /** @brief Aliveness and generations, holes included. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief Every row, holes included, for a whole-table walk. */
    [[nodiscard]] std::span<const domain::label> rows() const noexcept {
        return rows_;
    }

    void clear() noexcept;

private:
    slab layout_;
    std::vector<domain::label> rows_;
    std::unordered_map<std::string, label_handle> by_id_;
    std::unordered_map<std::string, label_handle> by_name_;
};

/**
 * @brief The label attachments, as a plain list of pairs.
 *
 * A linear scan is correct at this size and is simpler than an index: an
 * operator has tens of labels, an attachment is two strings, and every question
 * asked of this table is "which labels does this owner have" or "does this pair
 * exist", both of which a scan of a few dozen elements answers. The hand-rolled
 * buckets an index would need, and their invalidation on every attach, would
 * cost more than they save and be a second place for the pair identity to be
 * got wrong.
 */
class HEDGR_MODEL_EXPORT labelling_table final {
public:
    /** @brief Attaches @p label_id to the owner; does nothing if already
     * attached. */
    void add(domain::labelling_owner owner, std::string owner_id,
             std::string label_id);

    /** @brief Detaches @p label_id from the owner; does nothing if absent. */
    void remove(domain::labelling_owner owner, std::string owner_id,
                std::string label_id);

    /** @brief Detaches every label from the owner. */
    void remove_owner(domain::labelling_owner owner, std::string_view owner_id);

    /** @brief The label ids attached to the owner, in insertion order. */
    [[nodiscard]] std::vector<std::string>
    labels_of(domain::labelling_owner owner, std::string_view owner_id) const;

    /** @brief Every attachment, for a whole-table walk. */
    [[nodiscard]] const std::vector<domain::labelling>& rows() const noexcept {
        return rows_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    void clear() noexcept { rows_.clear(); }

private:
    std::vector<domain::labelling> rows_;
};

/**
 * @brief The shortcuts bar, kept in the order it draws in.
 *
 * A plain ordered vector rather than a slab with an index, because the order is
 * the data and an operator has tens of entries. Every question asked of this
 * table is "what is on the bar, in order" or "which entries name this target",
 * and a sorted vector answers both with no bookkeeping to invalidate. A save
 * moves at most the entries after the one it touches, which is the smallest
 * write a reorder can be.
 */
class HEDGR_MODEL_EXPORT shortcut_table final {
public:
    /**
     * @brief Adds @p row, or replaces the entry carrying the same id.
     *
     * The entry lands where its position and id put it rather than at the end,
     * so a caller that saves a lower position sees it take effect on the next
     * read instead of having to sort the bar itself.
     */
    void upsert(domain::shortcut row);

    /** @brief Drops the entry with @p id, if it is there. */
    void remove(std::string_view id);

    /**
     * @brief Drops every entry that opens @p target_id as @p kind.
     *
     * This is what a target removal calls, so an entry never outlives the row
     * it opens. The match is on both halves, because an instance and an account
     * can carry the same id and only one of them is the target being removed.
     */
    void remove_target(domain::window_kind kind, std::string_view target_id);

    /** @brief Every entry, in the order the bar draws them. */
    [[nodiscard]] const std::vector<domain::shortcut>& rows() const noexcept {
        return rows_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

    void clear() noexcept { rows_.clear(); }

private:
    std::vector<domain::shortcut> rows_;
};

/**
 * @brief The live columns of a session, split by how often they are read.
 *
 * The frame loop reads status for the connection indicator, so it
 * sits in the first block. The tenant, party and last error are read only when a
 * detail surface opens, so they sit last and never enter the per-frame walk.
 */
struct session_columns final {
    // Hot: read by the frame loop.
    std::vector<std::uint8_t> status;

    // Warm: read when a session row is drawn.
    std::vector<std::string> instance_id;
    std::vector<std::string> account_id;
    std::vector<std::uint8_t> mode;

    // Cold: read only when a detail surface opens.
    std::vector<std::int64_t> opened_at_ns;
    std::vector<std::string> last_error;
    std::vector<std::string> tenant_name;
    std::vector<std::string> tenant_id;
    std::vector<std::string> party_name;
    std::vector<std::string> party_id;
    std::vector<domain::secret_string> token;
    std::vector<std::string> parties;
    std::vector<domain::deployment_snapshot> deployment;
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

    [[nodiscard]] std::string_view last_error(session_handle h) const;
    void set_last_error(session_handle h, std::string value);

    /** @brief Records what IAM returned once the account was accepted. */
    void set_account_result(session_handle h, std::string tenant_name,
                             std::string party_name, std::string party_id,
                             domain::secret_string token);

    /** @brief Records the tenant's key, for reads scoped by tenant. */
    void set_tenant_id(session_handle h, std::string tenant_id);

    /** @brief Records the parties the login may act as, serialised. */
    void set_parties(session_handle h, std::string parties);

    /** @brief The parties the login may act as, serialised, or empty. */
    [[nodiscard]] std::string_view parties(session_handle h) const;

    /** @brief The context this session runs in, decided from its tenant. */
    [[nodiscard]] domain::session_mode mode(session_handle h) const;
    void set_mode(session_handle h, domain::session_mode value);

    /**
     * @brief The token IAM issued, for a read on this session's behalf.
     *
     * Empty until the session is authenticated. A caller that opens a second
     * connection to read something without disturbing this session's own
     * transport needs it, and it is the only accessor that hands a secret out.
     */
    [[nodiscard]] std::string_view token(session_handle h) const;

    /** @brief Replaces the token after IAM issues a fresh one. */
    void set_token(session_handle h, domain::secret_string token);

    /**
     * @brief The last deployment read for this session, if any.
     *
     * A snapshot rather than the counters alone, because the counts and the
     * roster have to be about the same moment. Empty before the first read.
     */
    [[nodiscard]] const domain::deployment_snapshot&
    deployment(session_handle h) const;
    void set_deployment(session_handle h, domain::deployment_snapshot value);

    /**
     * @brief When this session was established, on the monotonic clock.
     *
     * A fact about the session rather than about either end of it, so it lives
     * here and not on the account or the instance. Zero means the session has
     * never been authenticated, which is a different state from having been
     * established at the epoch.
     */
    [[nodiscard]] std::int64_t opened_at_ns(session_handle h) const;
    void set_opened_at_ns(session_handle h, std::int64_t value);

    [[nodiscard]] std::string_view instance_id(session_handle h) const;
    [[nodiscard]] std::string_view account_id(session_handle h) const;
    [[nodiscard]] std::string_view tenant_name(session_handle h) const;

    /** @brief Key of the tenant the session belongs to, or empty. */
    [[nodiscard]] std::string_view tenant_id(session_handle h) const;
    [[nodiscard]] std::string_view party_name(session_handle h) const;

    /** @brief Key of the party the session is scoped to, or empty. */
    [[nodiscard]] std::string_view party_id(session_handle h) const;

    /** @brief Aliveness and generations, for a handle-walking loop. */
    [[nodiscard]] const slab& layout() const noexcept { return layout_; }

    /** @brief The status column, stride-1, holes included. */
    [[nodiscard]] std::span<const std::uint8_t> status_column() const noexcept {
        return columns_.status;
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

    /*
     * The session the window was saved against, kept even while no session
     * exists. A restored window is unbound until its session signs in again,
     * and the pair it was saved against is what tells the runtime which sign-in
     * makes it live.
     */
    std::vector<std::string> session_instance;
    std::vector<std::string> session_account;

    /*
     * The series each window holds, one list per window and empty for a kind
     * that shows none. Written through window_table::set_series only, so the
     * window service is the one place that pairs a write with the table's
     * acquire and release.
     */
    std::vector<std::vector<series_handle>> series;
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

    /** @brief Asks the frame loop to raise a window on its next frame. */
    void request_focus(std::string_view id);

    /** @brief Takes the pending focus request, if there is one. */
    [[nodiscard]] std::string take_focus_request() const;

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

    /** @brief The series the window holds, or nothing for a kind with none. */
    [[nodiscard]] std::span<const series_handle>
    series(window_handle h) const noexcept;

    /** @brief Replaces the series the window holds. */
    void set_series(window_handle h, std::span<const series_handle> handles);

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

    mutable std::string focus_request_;
};

}

#endif
