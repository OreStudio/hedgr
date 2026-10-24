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
#include <catch2/catch_all.hpp>

namespace widget = hedgr::ui::widget;

TEST_CASE("the accounts filter matches name, principal, instance or tenant, in "
          "any case",
    "[accounts_panel]") {
    CHECK(widget::account_matches("", "Ops", "ops@acme", "Inst", "Acme"));
    CHECK(widget::account_matches("ops", "Ops", "ops@acme", "Inst", "Acme"));
    CHECK(widget::account_matches("OPS@", "Ops", "ops@acme", "Inst", "Acme"));
    CHECK(widget::account_matches("inst", "Ops", "ops@acme", "Inst", "Acme"));
    CHECK(widget::account_matches("ACME", "Ops", "ops@acme", "Inst", "Acme"));
    CHECK_FALSE(widget::account_matches("beta", "Ops", "ops@acme", "Inst",
        "Acme"));
    CHECK_FALSE(widget::account_matches("ops!", "Ops", "ops@acme", "Inst",
        "Acme"));
}

TEST_CASE("accounts sort by tenant, then instance, then name, with no tenant "
          "last",
    "[accounts_panel]") {
    const auto order = [](std::string_view tenant, std::string_view instance,
                              std::string_view name) {
        return widget::account_entry{
            .tenant = tenant, .name = name, .instance = instance};
    };

    /* Tenant first, folded, so "acme" and "Acme" sit together. */
    CHECK(widget::account_entry_less(order("Acme", "Inst", "Zed"),
        order("Beta", "Inst", "Alpha")));
    CHECK(widget::account_entry_less(order("acme", "Inst", "Zed"),
        order("Beta", "Inst", "Alpha")));
    CHECK_FALSE(widget::account_entry_less(order("Beta", "Inst", "Alpha"),
        order("Acme", "Inst", "Zed")));

    /* An account with no tenant goes after every account that has one. */
    CHECK(widget::account_entry_less(order("Acme", "Inst", "Zed"),
        order("", "Inst", "Alpha")));
    CHECK_FALSE(widget::account_entry_less(order("", "Inst", "Alpha"),
        order("Acme", "Inst", "Zed")));

    /* Within one tenant, the instance, and then the account's name. */
    CHECK(widget::account_entry_less(order("Acme", "Alpha", "Zed"),
        order("Acme", "Beta", "Alpha")));
    CHECK(widget::account_entry_less(order("Acme", "Inst", "Admin"),
        order("Acme", "Inst", "Ops")));
    CHECK_FALSE(widget::account_entry_less(order("Acme", "Inst", "Ops"),
        order("Acme", "Inst", "Admin")));
    CHECK_FALSE(widget::account_entry_less(order("Acme", "Inst", "Ops"),
        order("Acme", "Inst", "Ops")));
}

TEST_CASE("an empty tenant is headed no tenant yet", "[accounts_panel]") {
    CHECK(widget::tenant_heading("") == "No tenant yet");
    CHECK(widget::tenant_heading("Acme") == "Acme");
}
