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
#ifndef HEDGR_MODEL_STATE_CONDITION_TABLE_HPP
#define HEDGR_MODEL_STATE_CONDITION_TABLE_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::model::state {

/**
 * @brief What raised a condition, which is the first half of its cause.
 *
 * A source names the kind of thing that went wrong, and the subject beside it
 * in the key names which one. Together they are what a repeat of the same
 * failure updates and what the step that resolves it clears, so the list is
 * of causes an operator can act on, not of the functions that happened to
 * detect them.
 */
enum class condition_source : std::uint8_t {
    /** @brief The store could not be opened, migrated or read at start. */
    startup,
    instance,
    account,
    folder,
    label,
    /** @brief A label attached to, or detached from, its owner. */
    labelling,
    shortcut,
    /** @brief A connect or disconnect the operator asked for. */
    session,
    /** @brief A stored password for an account. */
    credential,
    /** @brief The master password and the lock it opens. */
    store_lock,
    /** @brief One window being opened, closed or configured. */
    window,
    /** @brief The saved wall of windows as a whole. */
    window_wall,
    /** @brief A read that could not start. */
    read,
    probe,
};

/**
 * @brief How long a condition from a source stands.
 *
 * The two lifetimes are the two things the status bar's red can mean. A
 * verdict answers the operator's last command, so the next command takes it
 * down whether or not that command is about the same thing: a removal that
 * succeeded after one that was refused would otherwise leave the bar reporting
 * a refusal the store did not have. A standing condition describes something
 * that is still true, such as a wall the store would not save, and only the
 * step that makes it untrue clears it.
 */
enum class condition_lifetime : std::uint8_t { verdict, standing };

/**
 * @brief The lifetime of every condition raised from @p source.
 *
 * Decided by the source rather than at each raise so that one site cannot
 * disagree with another about whether the same cause fades. Startup stands
 * because nothing this process does can bring up a store it refused, and the
 * wall stands because an unsaved layout is true until a save succeeds.
 */
[[nodiscard]] constexpr condition_lifetime
lifetime_of(condition_source source) noexcept {
    switch (source) {
    case condition_source::startup:
    case condition_source::window_wall:
        return condition_lifetime::standing;
    default:
        return condition_lifetime::verdict;
    }
}

/**
 * @brief Weight of a condition when the table picks the one to show.
 *
 * Declared in ascending order, so the greater value is the graver one.
 */
enum class condition_severity : std::uint8_t { warning, error };

/**
 * @brief The cause of a condition: what kind of thing failed, and which one.
 *
 * The subject is the id the operator would recognise, such as a folder id, an
 * account id or a window id, and is empty for a source that has only one
 * instance, such as the store lock.
 */
struct condition_key final {
    condition_source source{};
    std::string subject;

    [[nodiscard]] bool operator==(const condition_key&) const = default;
};

/** @brief One failure the operator has not yet seen resolved. */
struct condition final {
    condition_key key;
    condition_severity severity{condition_severity::error};
    std::string message;
    /** @brief When it was last raised, on the monotonic clock. */
    std::int64_t raised_ns{};
};

/**
 * @brief The failures the runtime has recorded and not yet seen resolved.
 *
 * One table in place of one string, which is what lets two failures in the
 * same frame both be kept: a store refusal and a certificate failure used to
 * share a slot, and the second overwrote the first before anyone had read it.
 *
 * Rows are keyed by cause. Raising the same cause again updates its row rather
 * than adding one, so a failure that repeats every poll is one line and not a
 * growing list, and the row moves to the back so that "the newest" is the one
 * most recently seen. Clearing is by cause too, by a key or by a whole source,
 * and the lifetime rule in @c lifetime_of decides which rows a new command
 * takes down.
 *
 * The table is bounded. When it is full the oldest row goes, because a bar
 * that can show one line has no use for the thirty-third, and a runtime that
 * raises faster than the operator clears would otherwise grow without limit.
 *
 * Owned and written by the frame thread, like every other table.
 */
class HEDGR_MODEL_EXPORT condition_table final {
public:
    static constexpr std::size_t default_capacity = 32;

    /** @brief A table holding at most @p capacity rows, which is at least one. */
    explicit condition_table(std::size_t capacity = default_capacity) noexcept;

    /**
     * @brief Records @p message as the state of @p key, raised at @p now_ns.
     *
     * Updates the row for @p key when one exists and appends it otherwise.
     * Either way the row becomes the newest. When the table is full the
     * oldest row is dropped to make room.
     */
    void raise(condition_key key, condition_severity severity,
        std::string message, std::int64_t now_ns);

    /** @brief Removes the row for @p key. Returns whether there was one. */
    bool clear(const condition_key& key);

    /** @brief Removes every row raised from @p source. Returns how many. */
    std::size_t clear(condition_source source);

    /**
     * @brief Removes every verdict, which is what a new command does.
     *
     * Rows whose source stands, per @c lifetime_of, are left alone. Returns
     * how many rows went.
     */
    std::size_t clear_verdicts();

    /**
     * @brief The one condition a single-line surface should show, or nullptr.
     *
     * The gravest severity wins, and among rows of that severity the one
     * raised most recently. Newest rather than oldest because the operator's
     * latest action is what they are looking at, and a re-raise counts as
     * new, so a failure that is still happening is the one on show.
     */
    [[nodiscard]] const condition* current() const noexcept;

    /** @brief The message of @c current, or empty when there is none. */
    [[nodiscard]] std::string_view message() const noexcept;

    /** @brief Every row, oldest raise first. */
    [[nodiscard]] std::span<const condition> rows() const noexcept {
        return rows_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] bool empty() const noexcept { return rows_.empty(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_;
    std::vector<condition> rows_;
};

}

#endif
