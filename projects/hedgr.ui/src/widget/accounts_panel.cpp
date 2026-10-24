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
#include "hedgr.ui/widget/accounts_panel.hpp"

namespace hedgr::ui::widget {

namespace {

[[nodiscard]] char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool contains_folded(std::string_view text,
    std::string_view needle) noexcept {
    if (needle.size() > text.size())
        return false;
    for (std::size_t at = 0; at + needle.size() <= text.size(); ++at) {
        std::size_t i = 0;
        while (i < needle.size() && fold(text[at + i]) == fold(needle[i]))
            ++i;
        if (i == needle.size())
            return true;
    }
    return false;
}

/** @brief Whether @p lhs is less than @p rhs, folding ASCII case. */
[[nodiscard]] bool folded_less(std::string_view lhs,
    std::string_view rhs) noexcept {
    const std::size_t shared = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
    for (std::size_t i = 0; i < shared; ++i) {
        const char a = fold(lhs[i]);
        const char b = fold(rhs[i]);
        if (a != b)
            return a < b;
    }
    return lhs.size() < rhs.size();
}

}

bool account_matches(std::string_view filter, std::string_view name,
    std::string_view principal, std::string_view instance,
    std::string_view tenant) noexcept {
    return filter.empty() || contains_folded(name, filter)
        || contains_folded(principal, filter)
        || contains_folded(instance, filter)
        || contains_folded(tenant, filter);
}

bool account_entry_less(const account_entry& lhs,
    const account_entry& rhs) noexcept {
    /*
     * A row with no tenant goes after every row with one. The empty tenant is
     * not a name that sorts before "a"; it is the absence of the answer, and
     * leading with it would bury the rows that have something to group under.
     */
    if (lhs.tenant.empty() != rhs.tenant.empty())
        return !lhs.tenant.empty();
    if (folded_less(lhs.tenant, rhs.tenant))
        return true;
    if (folded_less(rhs.tenant, lhs.tenant))
        return false;
    if (folded_less(lhs.instance, rhs.instance))
        return true;
    if (folded_less(rhs.instance, lhs.instance))
        return false;
    if (folded_less(lhs.name, rhs.name))
        return true;
    if (folded_less(rhs.name, lhs.name))
        return false;
    /*
     * Two accounts can share a tenant, an instance and a name; the slot is the
     * last resort, so the order is total and a frame draws the same list as the
     * one before it.
     */
    return lhs.slot < rhs.slot;
}

std::string_view tenant_heading(std::string_view tenant) noexcept {
    return tenant.empty() ? std::string_view{"No tenant yet"} : tenant;
}

}
