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
#ifndef HEDGR_UI_WIDGET_ACCOUNTS_PANEL_HPP
#define HEDGR_UI_WIDGET_ACCOUNTS_PANEL_HPP

#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/** @brief The size of the accounts filter's text buffer, terminator included. */
inline constexpr std::size_t account_filter_capacity = 64;

/**
 * @brief The text typed into the Accounts panel's filter box.
 *
 * A fixed buffer owned by the menu state, so ImGui edits it in place and the
 * frame allocates nothing. The panel clears it each time it opens.
 */
struct account_filter final {
    char text[account_filter_capacity] = {};

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(text);
    }
};

/**
 * @brief Whether an account passes @p filter.
 *
 * The filter is a case-insensitive substring over the account's display name,
 * its principal, the name of the instance it belongs to and the tenant its
 * last sign-in reached. ASCII is folded and other bytes compare as they are.
 * An empty filter passes everything. Nothing is allocated.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool account_matches(std::string_view filter,
    std::string_view name, std::string_view principal,
    std::string_view instance, std::string_view tenant) noexcept;

/**
 * @brief One account in the panel's draw order.
 *
 * The views borrow from the account, session and instance tables for the frame
 * they were built in. The entry is rebuilt every frame, so a view never
 * outlives the row it names.
 */
struct account_entry final {
    /** @brief The account's slab index, for pushing its id as the row's ImGui id. */
    std::uint32_t slot = 0;

    /** @brief The tenant the account's last sign-in reached, or empty. */
    std::string_view tenant;

    /** @brief The name the account is read by: its name, or its principal. */
    std::string_view name;

    /** @brief The principal IAM sees. */
    std::string_view principal;

    /** @brief The name of the instance the account belongs to. */
    std::string_view instance;
};

/**
 * @brief Orders entries by tenant, then instance, then account name.
 *
 * The order is case-insensitive, so "acme" and "Acme" sit together, and an
 * account with no tenant sorts last rather than first: a tenant nobody has
 * reached is the exception, not the head of the list. The account's slot breaks
 * a complete tie, so the order is total and stable between frames.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool account_entry_less(const account_entry& lhs,
    const account_entry& rhs) noexcept;

/** @brief The heading a tenant is drawn under, or the no-tenant heading. */
[[nodiscard]] HEDGR_UI_EXPORT std::string_view
tenant_heading(std::string_view tenant) noexcept;

/**
 * @brief What the Accounts panel keeps between frames.
 *
 * The filter text is a fixed buffer ImGui edits in place. The entries are
 * rebuilt on every frame the panel draws, into a vector kept across frames so
 * that a steady frame allocates nothing beyond what the sort needs. There is
 * no version to compare because the tenant a row is grouped under comes from
 * the session table, which has no version of its own: a sign-in that lands
 * between two frames must move its account to a new heading on the next one.
 */
struct accounts_panel_state final {
    /** @brief The text in the filter box. */
    account_filter filter;

    /** @brief Whether the panel was drawn last frame, to see it open. */
    bool shown = false;

    /** @brief The accounts that pass the filter, in draw order. */
    std::vector<account_entry> entries;

    /** @brief How many accounts there are, for the count beside the filter. */
    std::size_t total = 0;
};

}

#endif
